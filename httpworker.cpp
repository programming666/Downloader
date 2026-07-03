#include "httpworker.h"
#include "logger.h"
#include <QTimer>
#include <QThread>
#include <QApplication>
#include <QPointer>

/**
 * @brief HTTP下载工作线程构造函数
 * @param url 下载文件URL
 * @param filePath 文件保存路径
 * @param startPoint 下载起始字节位置
 * @param endPoint 下载结束字节位置
 *
 * 初始化HTTP下载工作线程，设置网络请求和文件操作
 * 支持HTTP Range请求实现断点续传和多线程下载
 * 设置合理的HTTP头信息模拟浏览器行为
 */
HttpWorker::HttpWorker(const QUrl& url, const QString& filePath, qint64 startPoint, qint64 endPoint)
    : QObject(nullptr),
      m_url(url),
      m_filePath(filePath),
      m_startPoint(startPoint),
      m_endPoint(endPoint),
      m_bytesReceived(0),
      m_netManager(nullptr),
      m_reply(nullptr),
      m_file(nullptr),
      m_isStopped(false),
      m_retryCount(0),
      m_alreadyFinished(false)
{
    LOGD(QString("构造HttpWorker - URL:%1 文件路径:%2 范围:%3-%4").arg(url.toString()).arg(filePath).arg(startPoint).arg(endPoint));
    setAutoDelete(false); // 不自动删除，由DownloadTask管理
    LOGD("HttpWorker构造完成，设置为手动删除模式");
}

/**
 * @brief 重置HttpWorker状态，允许重新启动下载（用于断点续传）
 *
 * 把 m_isStopped / m_retryCount / m_alreadyFinished 复位，
 * 同时把 m_resumeOffset 复位（避免 retry 把 resume offset 累计算两次）。
 * m_bytesReceived 在 startDownload() 中重新从文件大小同步。
 */
void HttpWorker::reset()
{
    m_isStopped = false;
    m_retryCount = 0;
    m_alreadyFinished = false;
    m_resumeOffset = 0;
    m_bytesReceived.store(0, std::memory_order_release);
    LOGD(QString("重置HttpWorker状态 - 文件:%1 范围:%2-%3")
         .arg(m_filePath).arg(m_startPoint).arg(m_endPoint));
}

HttpWorker::~HttpWorker()
{
    LOGD(QString("开始析构HttpWorker - 文件:%1").arg(m_filePath));

    // 先断开网络应答信号连接，避免异步事件触发已析构对象的槽函数。
    // 注意：在析构函数中用 disconnect(this) 会传入一个已部分析构的 this 指针，
    // Qt 内部按 sender==this 解码时会读 vptr，风险较高。这里用无参 disconnect()
    // 断开所有连接到 m_reply 的信号，避免触碰 this。
    if (m_reply) {
        LOGD("中止网络请求并断开信号");
        m_reply->disconnect(); // 无参版本：断开所有连接到此对象的信号槽
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    // 确保所有资源都被清理
    if (m_file && m_file->isOpen()) {
        LOGD("关闭文件");
        m_file->close();
    }
    if (m_file) {
        LOGD("删除文件对象");
        delete m_file;
        m_file = nullptr;
    }
    if (m_netManager) {
        LOGD("标记网络管理器为延迟删除");
        m_netManager->deleteLater();
        m_netManager = nullptr;
    }

    LOGD("HttpWorker析构完成");
}

void HttpWorker::run()
{
    LOGD(QString("HttpWorker::run 在线程池中执行下载任务，范围:%1-%2").arg(m_startPoint).arg(m_endPoint));
    LOGD(QString("当前线程:%1 主线程:%2")
         .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThread()), 16))
         .arg(QString::number(reinterpret_cast<quintptr>(qApp->thread()), 16)));

    if (m_isStopped) {
        LOGD("任务已停止，直接退出run方法");
        return;
    }

    // 关键：把 QObject 的事件循环亲和切到当前线程池线程。
    // 不切的话，QNetworkAccessManager 与 QNetworkReply 的 readyRead/finished/
    // metaDataChanged 信号会按 this->thread() == 主线程派发，导致 N 个 worker
    // 的高频 IO 事件全部排回主线程，把 UI 冻死。
    moveToThread(QThread::currentThread());

    // 跑一个本地事件循环消化 QNetworkReply 信号。
    // 退出条件：m_alreadyFinished（onFinished / onErrorOccurred 翻 true）或
    // m_isStopped（外部 stop() 路径），二者都会调 quitLoop() 退出循环。
    m_loop = new QEventLoop();

    LOGD("开始调用startDownload()");
    startDownload();
    LOGD("startDownload()调用完成，进入事件循环");

    // exec() 阻塞到 quitLoop() 调用。stop() 走 BlockingQueuedConnection 时会
    // 在 worker 线程同步调 quitLoop()，所以这个 exec 会从 stop 路径返回。
    m_loop->exec();
    LOGD("事件循环退出");

    delete m_loop;
    m_loop = nullptr;
    LOGD("HttpWorker::run 退出");
}

/**
 * @brief 开始HTTP下载任务
 *
 * 执行实际的HTTP下载操作：
 * 1. 打开目标文件准备写入
 * 2. 定位文件指针到指定位置
 * 3. 发送HTTP GET请求
 * 4. 连接网络响应信号
 * 5. 开始数据接收
 *
 * 支持断点续传，通过Range头指定下载范围
 * 文件操作失败时会发射错误信号
 *
 * 线程模型：run() 入口已经把 this 切到当前线程池线程（moveToThread），所以
 * 这里的 new QNetworkAccessManager / QNetworkReply 都在 worker 线程，readyRead
 * / finished 信号会派发到 worker 线程的事件循环里消化，不会再排回主线程。
 */
void HttpWorker::startDownload()
{
    LOGD(QString("开始网络下载，范围:%1-%2").arg(m_startPoint).arg(m_endPoint));

    if (m_isStopped) {
        LOGD("任务已停止，退出startDownload");
        cleanup();
        return;
    }

    LOGD(QString("当前线程:%1 主线程:%2")
         .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThread()), 16))
         .arg(QString::number(reinterpret_cast<quintptr>(qApp->thread()), 16)));

    // 已在 worker 线程（run() 入口 moveToThread 过），直接 new QNAM。
    LOGD("在 worker 线程中创建 QNetworkAccessManager");
    m_netManager = new QNetworkAccessManager();
    LOGD("QNetworkAccessManager创建完成，开始网络请求");
    continueDownload();
}

void HttpWorker::continueDownload()
{

    LOGD(QString("开始创建文件对象:%1").arg(m_filePath));
    // 防御性清理：resume/retry 时 m_file 可能指向旧指针（被前面的 run 流程创建过），
    // 避免 new QFile 覆盖导致旧对象泄漏
    if (m_file) {
        if (m_file->isOpen()) {
            m_file->close();
        }
        delete m_file;
        m_file = nullptr;
    }
    m_file = new QFile(m_filePath);
    LOGD("文件对象创建完成");

    // 检查是否需要断点续传：单独记录 resume offset，避免 m_bytesReceived 含义混淆
    LOGD(QString("检查文件是否存在:%1").arg(m_file->exists() ? "存在" : "不存在"));
    if (m_file->exists()) {
        const qint64 existingSize = m_file->size();
        m_resumeOffset = existingSize;
        m_bytesReceived.store(existingSize, std::memory_order_release);
        LOGD(QString("文件已存在，大小:%1，使用追加模式").arg(existingSize));
        if (!m_file->open(QIODevice::Append)) {
            LOGD(QString("无法打开文件进行追加，错误:%1").arg(m_file->errorString()));
            emit error(tr("无法打开临时文件进行追加: %1").arg(m_file->errorString()));
            cleanup();
            return;
        }
        LOGD("文件以追加模式打开成功");
    } else {
        m_resumeOffset = 0;
        LOGD("文件不存在，创建新文件");
        if (!m_file->open(QIODevice::WriteOnly)) {
            LOGD(QString("无法创建文件，错误:%1").arg(m_file->errorString()));
            emit error(tr("无法创建临时文件: %1").arg(m_file->errorString()));
            cleanup();
            return;
        }
        LOGD("新文件创建成功");
    }

    qint64 currentStartPoint = m_startPoint + m_resumeOffset;
    LOGD(QString("计算当前开始点:%1 (原始开始点:%2 + 已存在字节数:%3)")
         .arg(currentStartPoint).arg(m_startPoint).arg(m_resumeOffset));

    // 如果这个分块已经下载完成（endPoint==-1 表示整文件下载，没有结束点，跳过判断）
    if (m_endPoint >= 0 && currentStartPoint > m_endPoint) {
        LOGD(QString("分块已完成下载，当前开始点:%1 > 结束点:%2").arg(currentStartPoint).arg(m_endPoint));
        m_file->close();
        cleanup();
        if (!m_alreadyFinished) {
            m_alreadyFinished = true;
            emit finished();
        }
        return;
    }

    QNetworkRequest request(m_url);
    // 仅当 endPoint != -1（已知结束字节）时设置 Range 头；endPoint==-1 表示未知长度，
    // 不发 Range 头，让服务器返回完整文件
    const bool useRange = (m_endPoint >= 0);
    if (useRange) {
        QString rangeHeader = QString("bytes=%1-%2").arg(currentStartPoint).arg(m_endPoint);
        request.setRawHeader("Range", rangeHeader.toUtf8());
        LOGD(QString("设置Range头:%1").arg(rangeHeader));
    } else {
        LOGD("endPoint=-1，整文件下载，不设置Range头");
    }
    request.setTransferTimeout(30000); // 30秒超时

    // 设置User-Agent，避免被网站屏蔽
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");

    // 设置其他常用头部
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9,zh-CN;q=0.8,zh;q=0.7");
    request.setRawHeader("Connection", "keep-alive");

    LOGD("发送网络请求...");
    LOGD("开始调用m_netManager->get()...");
    m_reply = m_netManager->get(request);

    // null-check：QNetworkAccessManager::get 理论上不会返回 nullptr，但加上
    // 防御性检查避免后续 connect() 解引用 nullptr 直接崩溃
    if (!m_reply) {
        LOGD("m_netManager->get() 返回 nullptr，发射错误信号");
        emit error(tr("无法创建网络请求(QNetworkAccessManager::get 返回空)"));
        cleanup();
        return;
    }
    LOGD("m_netManager->get()调用完成，开始连接信号...");

    connect(m_reply, &QNetworkReply::readyRead, this, &HttpWorker::onReadyRead);
    LOGD("readyRead信号连接完成");
    connect(m_reply, &QNetworkReply::finished, this, &HttpWorker::onFinished);
    LOGD("finished信号连接完成");
    connect(m_reply, &QNetworkReply::errorOccurred, this, &HttpWorker::onErrorOccurred);
    LOGD("errorOccurred信号连接完成");

    // 部分服务器会无视 Range 直接返回 200 + 完整数据。如果不检查就把 Range 内容追加到
    // 已存在字节之后，分片文件会变成"原已下载字节 + 完整文件字节"，合并后必坏。
    // 所以用 lambda 监听 metaDataChanged，一旦拿到响应头就立刻判断状态码和 Content-Range。
    if (useRange) {
        QPointer<HttpWorker> safeThis(this);
        QPointer<QNetworkReply> safeReply(m_reply);
        QObject::connect(m_reply, &QNetworkReply::metaDataChanged, this, [safeThis, safeReply]() {
            if (!safeThis || !safeReply) {
                return;
            }
            const int statusCode = safeReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // 服务器忽略 Range：返回 200 + 完整内容
            if (statusCode == 200) {
                LOGD(QString("服务器忽略Range头(返回200 OK)，丢弃已下载字节并以单线程整文件模式重试"));
                // 截断已存在的部分文件，重新整文件下载
                if (safeThis->m_file) {
                    safeThis->m_file->close();
                    QFile::remove(safeThis->m_filePath);
                }
                // 把 worker 切回单文件模式：把 endPoint 设为 -1 让下一次请求不带 Range；
                // 保留 m_startPoint==0，仅重置 resume offset 和 progress 计数
                safeThis->m_endPoint = -1;
                safeThis->m_startPoint = 0;
                safeThis->m_resumeOffset = 0;
                safeThis->m_bytesReceived.store(0, std::memory_order_release);
                // 标记"预期内的 cancel"：下面的 abort() 会同步触发
                // errorOccurred(OperationCanceledError)，onErrorOccurred 看到这个标志后
                // 只 quitLoop 走人，让 QTimer::singleShot 排队的 continueDownload()
                // 真正开始整文件重试。少了这个标志，error 回调会 emit error
                // 把整条 DownloadTask 拖进 Failed。
                safeThis->m_alreadyFinished = true;
                safeReply->abort();
                safeReply->deleteLater();
                safeThis->m_reply = nullptr;
                // 重新发起请求
                QTimer::singleShot(0, safeThis, [safeThis]() {
                    if (safeThis && !safeThis->m_isStopped) {
                        // 整文件重试：清掉 m_alreadyFinished 让 onFinished 正常 emit finished
                        safeThis->m_alreadyFinished = false;
                        safeThis->continueDownload();
                    }
                });
                return;
            }
            // 部分服务器可能返回 200 但 Content-Range 表明完整范围，这种情况下
            // 也需要走单文件重试路径
            if (statusCode == 206) {
                const QByteArray contentRange = safeReply->rawHeader("Content-Range");
                // Content-Range: bytes start-end/total
                const QString cr = QString::fromLatin1(contentRange);
                const int dashIdx = cr.indexOf('-');
                if (dashIdx > 0) {
                    const qint64 returnedStart = cr.left(dashIdx).toLongLong();
                    if (returnedStart != safeThis->m_startPoint + safeThis->m_resumeOffset) {
                        LOGD(QString("Content-Range起始字节(%1)与请求不符(期望%2)，丢弃已下载字节")
                             .arg(returnedStart).arg(safeThis->m_startPoint + safeThis->m_resumeOffset));
                        if (safeThis->m_file) {
                            safeThis->m_file->close();
                            QFile::remove(safeThis->m_filePath);
                        }
                        safeThis->m_endPoint = -1;
                        safeThis->m_startPoint = 0;
                        safeThis->m_resumeOffset = 0;
                        safeThis->m_bytesReceived.store(0, std::memory_order_release);
                        // 标记"预期内的 cancel"：见上面 200 路径注释
                        safeThis->m_alreadyFinished = true;
                        safeReply->abort();
                        safeReply->deleteLater();
                        safeThis->m_reply = nullptr;
                        QTimer::singleShot(0, safeThis, [safeThis]() {
                            if (safeThis && !safeThis->m_isStopped) {
                                // 整文件重试：清掉 m_alreadyFinished 让 onFinished 正常 emit finished
                                safeThis->m_alreadyFinished = false;
                                safeThis->continueDownload();
                            }
                        });
                    }
                }
            }
        });
    }

    LOGD("网络请求已发送，等待异步响应...");
    LOGD("continueDownload方法执行完成");
}

void HttpWorker::cleanup()
{
    LOGD("开始清理HttpWorker资源");
    
    if (m_file && m_file->isOpen()) {
        LOGD("关闭文件");
        m_file->close();
    }
    if (m_reply) {
        LOGD("断开网络回复信号连接");
        m_reply->disconnect();
        LOGD("标记网络回复为延迟删除");
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_netManager) {
        LOGD("标记网络管理器为延迟删除");
        m_netManager->deleteLater();
        m_netManager = nullptr;
    }
    
    LOGD("HttpWorker资源清理完成");
}

void HttpWorker::stopAsync()
{
    // 比较"是否在主线程"而不是"是否在构造时所属线程"。HttpWorker 的父对象是 nullptr，
    // this->thread() 在 run() 中会被 QThreadPool 切换为线程池线程，stop() 调用点
    // 通常是 main thread。比较 this->thread() 总是 false → 总是走 invokeMethod 路径，
    // 看起来能跑但语义不对。统一改为 qApp->thread()。
    if (QThread::currentThread() == qApp->thread()) {
        stop();
    } else {
        QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection);
    }
}

/**
 * @brief 退出 run() 内部的事件循环。
 *  - 在 worker 线程里直接 quit()；
 *  - 从其他线程（主线程 cancel/pause）调用时通过 invokeMethod 切到 worker 线程。
 *  quit() 异步生效；调用方需要等 run() 返回（cancel 路径会 waitForDone）。
 */
void HttpWorker::quitLoop()
{
    if (!m_loop) {
        return;
    }
    if (QThread::currentThread() == this->thread()) {
        if (m_loop->isRunning()) {
            m_loop->quit();
        }
    } else {
        // 跨线程：把 quit 投递到 worker 线程的事件循环
        QMetaObject::invokeMethod(this, [this]() {
            if (m_loop && m_loop->isRunning()) {
                m_loop->quit();
            }
        }, Qt::QueuedConnection);
    }
}

/**
 * @brief 把新代理应用到 worker 的 QNAM。
 *
 * 必须在主线程调用（DownloadTask::applyProxy 的调用约定）。
 * 由于 HttpWorker 的 QNAM 由 startDownload() 在主线程通过
 * QTimer::singleShot(0, qApp, ...) 构造，因此从主线程直接调用 setProxy
 * 是 Qt 文档保证的线程安全操作。已发出去的请求不会被中断，但下一条
 * 请求（包含重试路径上由 continueDownload() 重新发起的）会用新代理。
 */
void HttpWorker::setProxy(const QNetworkProxy& proxy)
{
    if (!m_netManager) {
        // QNAM 还没创建（worker 仍在 Pending 或 stop 路径上没有跑过 run()）。
        // 这种情况下，无需做任何事——startDownload() 后续创建 QNAM 时不会读取
        // 我们这里的任何状态，调用方会通过 DownloadTask::m_proxy 兜底；
        // 不过 m_netManager 是在 run() 的 QTimer::singleShot(0, qApp, ...) 里
        // 才创建的，所以这里读不到是预期。什么都不做即可。
        return;
    }
    m_netManager->setProxy(proxy);
}

/**
 * @brief 停止下载。
 * 线程安全：
 *  - 不论 caller 在哪个线程，都要把 m_reply 的 abort 切到 worker 线程执行
 *    （QNetworkReply 的 abort/readAll 不是线程安全的，必须在 this->thread() 调）。
 *  - 用 BlockingQueuedConnection 让 caller 等 worker 线程完成 abort，
 *    避免 caller 析构 worker 时 reply 还在飞。
 */
void HttpWorker::stop()
{
    LOGD("停止HttpWorker");
    m_isStopped = true;

    // 立即关闭文件：QFile::close 在 worker 线程里跑（this->thread() == worker 线程），
    // 文件对象的所有权在 worker 线程，跨线程 close 会触发 QFile 的线程警告，
    // 所以这里只在自己线程里 close。
    const QThread* myThread = this->thread();
    if (QThread::currentThread() == myThread) {
        if (m_file && m_file->isOpen()) {
            LOGD("立即关闭文件");
            m_file->close();
        }
    }

    if (QThread::currentThread() == myThread) {
        // 在 worker 线程里直接处理 reply
        if (m_reply) {
            if (m_reply->isRunning()) {
                LOGD("直接中止网络请求并清空缓冲区");
                m_reply->abort();
                m_reply->readAll();
            }
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        quitLoop();
    } else {
        // 从其他线程（通常是主线程）调过来，把 abort 切到 worker 线程并等返回
        LOGD("跨线程停止HttpWorker，调度到 worker 线程");
        QMetaObject::invokeMethod(this, [this]() {
            if (m_reply) {
                if (m_reply->isRunning()) {
                    LOGD("通过 worker 线程中止网络请求并清空缓冲区");
                    m_reply->abort();
                    m_reply->readAll();
                }
                m_reply->deleteLater();
                m_reply = nullptr;
            }
            if (m_file && m_file->isOpen()) {
                m_file->close();
            }
            quitLoop();
        }, Qt::BlockingQueuedConnection);
    }

    LOGD("HttpWorker完全停止");
}

void HttpWorker::onReadyRead()
{
    // 更严格的停止检查
    if (m_isStopped) {
        LOGD("收到数据但任务已停止，丢弃数据");
        if (m_reply) m_reply->readAll(); // 清空缓冲区
        return;
    }

    // m_reply 可能被 cleanup()/stop() 在事件循环间隙置空，但 Qt 仍投递了已经
    // enqueue 的 readyRead 信号。安全起见在这里 null-check。
    if (!m_reply) {
        LOGD("onReadyRead 但 m_reply 为空，跳过");
        return;
    }

    if (!m_file || !m_file->isOpen()) {
        LOGD(QString("文件不可用 - 文件对象:%1 文件打开:%2")
             .arg(m_file ? "存在" : "不存在")
             .arg((m_file && m_file->isOpen()) ? "是" : "否"));
        return;
    }

    QByteArray data = m_reply->readAll();
    qint64 dataSize = data.size();

    if (dataSize > 0) {
        qint64 written = m_file->write(data);
        if (written != dataSize) {
            LOGD(QString("文件写入不完整，期望:%1 实际:%2").arg(dataSize).arg(written));
        }
        // 使用 std::atomic 的 fetch_add（语义等于旧的 fetchAndAddRelease），无需再 store
        m_bytesReceived.fetch_add(dataSize, std::memory_order_release);
        emit progress(dataSize);

        // 每接收1MB数据记录一次日志，避免日志过多
        const qint64 curBytes = m_bytesReceived.load(std::memory_order_acquire);
        if (curBytes - m_lastLoggedBytes >= 1024 * 1024) {
            LOGD(QString("接收数据进度 - 本次:%1字节 总计:%2字节").arg(dataSize).arg(curBytes));
            m_lastLoggedBytes = curBytes;
        }
    }
}

void HttpWorker::onFinished()
{
    LOGD("网络请求完成，开始处理结果...");

    if (m_isStopped) {
        LOGD("任务已停止，清理资源并退出");
        cleanup();
        quitLoop();
        return;
    }

    // 防御性 null-check：stop()/cleanup() 在事件循环回到主线程后可能把 m_reply
    // 置空。Qt 仍然可能把已 enqueue 的 finished 信号投递过来。
    if (!m_reply) {
        LOGD("onFinished 但 m_reply 为空，跳过处理");
        quitLoop();
        return;
    }

    if (m_reply->error() == QNetworkReply::NoError) {
        LOGD(QString("下载成功完成 - 总接收字节数:%1").arg(m_bytesReceived.load(std::memory_order_acquire)));
        if (!m_alreadyFinished) {
            m_alreadyFinished = true;
            emit finished();
        }
    } else {
        LOGD(QString("下载出现错误 - 错误码:%1 错误信息:%2").arg(m_reply->error()).arg(m_reply->errorString()));
        if (!m_alreadyFinished) {
            m_alreadyFinished = true;
            emit error(m_reply->errorString());
        }
    }

    // 如果上层已经标 m_alreadyFinished=true（metaDataChanged abort 触发的重试路径），
    // 这里不要 cleanup + quitLoop：QTimer::singleShot 排的 continueDownload() 必须
    // 还在 event loop 里派发，否则整文件重试永远不发生。
    if (m_alreadyFinished) {
        LOGD("m_alreadyFinished=true（metaDataChanged 重试路径），跳过 cleanup/quitLoop 让重试继续");
        return;
    }

    LOGD("开始清理资源");
    cleanup();
    quitLoop();
    LOGD("onFinished处理完成");
}

void HttpWorker::onErrorOccurred(QNetworkReply::NetworkError code)
{
    LOGD(QString("网络错误发生 - 错误码:%1").arg(code));

    // 防御性 null-check：onErrorOccurred 可能在 stop() 把 m_reply 置空后仍被 Qt 内部派发
    QString errorString = m_reply ? m_reply->errorString() : QStringLiteral("未知错误");
    LOGD(QString("错误详细信息:%1").arg(errorString));

    // metaDataChanged lambda 在"server 忽略 Range / Content-Range 起点不符"路径
    // 上会自己调 safeReply->abort() 切到整文件重试，abort() 同步触发
    // errorOccurred(OperationCanceledError)。如果让这里继续走
    // "非重试 / emit error" 路径，会让 DownloadTask 把这次自己人触发的 cancel
    // 当成"真实错误"并 cancel 掉所有其他 worker（part1 抛锚拖死 part0/part2..7）。
    // 识别规则：m_alreadyFinished==true 表示上层已经表态"这次终止是预期的"。
    // 注意：这里 **不要** quitLoop，QTimer::singleShot 排队了 continueDownload()
    // 真正开始整文件重试，event loop 必须继续跑才能派发那个 timer。
    if (code == QNetworkReply::OperationCanceledError && m_alreadyFinished) {
        LOGD("OperationCanceledError 是上层主动触发（metaDataChanged 重试），不视为错误，继续跑 event loop 等 QTimer 派发重试");
        return;
    }

    if (m_isStopped && code == QNetworkReply::OperationCanceledError) {
        // 用户主动停止，不是错误
        LOGD("用户主动停止下载，这不是错误");
        if (!m_alreadyFinished) {
            m_alreadyFinished = true;
            emit finished();
        }
        cleanup();
        quitLoop();
        return;
    }

    // 检查是否需要重试（网络相关错误），使用实例成员避免跨worker共享
    static constexpr int kMaxRetries = 3;

    if (m_retryCount < kMaxRetries &&
        (code == QNetworkReply::ConnectionRefusedError ||
         code == QNetworkReply::RemoteHostClosedError ||
         code == QNetworkReply::TimeoutError ||
         code == QNetworkReply::TemporaryNetworkFailureError)) {

        m_retryCount++;
        LOGD(QString("网络错误，尝试第%1次重试...").arg(m_retryCount));

        // 清理当前资源：先 abort 让底层 socket 立即关闭，再断开信号，
        // 最后 deleteLater。删除期间 onErrorOccurred 可能再次触发，靠 m_isStopped
        // 标志 + m_reply 置空防止重入。
        if (m_reply) {
            m_reply->abort(); // 先 abort 停止底层 IO
            m_reply->disconnect(this); // 断开所有信号连接，避免重试期间再次回调
            m_reply->deleteLater();
            m_reply = nullptr;
        }

        // 延迟重试：通过 QTimer::singleShot(0, ...) 调度到事件循环，避免
        // 直接在 onErrorOccurred（worker 线程上下文）里同步重入 run() 而把
        // worker 线程池调用栈打乱。重试时再次检查 m_isStopped。如果 worker
        // 已被 stop，重试不执行。
        // safeThis 亲和是 worker 线程（run() 入口 moveToThread 过），
        // QTimer 会在 worker 线程事件循环里派发。
        QPointer<HttpWorker> safeThis(this);
        QTimer::singleShot(2000 * m_retryCount, safeThis, [safeThis]() {
            if (!safeThis) {
                return;
            }
            if (safeThis->m_isStopped) {
                LOGD("重试前发现 worker 已停止，放弃重试");
                safeThis->cleanup();
                return;
            }
            LOGD("执行重试下载");
            // 已在 worker 线程，startDownload() 直接 new QNetworkAccessManager。
            safeThis->startDownload();
        });

        return;
    }

    // 重置重试计数器
    m_retryCount = 0;

    LOGD("发射错误信号并清理资源");
    if (!m_alreadyFinished) {
        m_alreadyFinished = true;
        emit error(errorString);
    }
    cleanup();
    quitLoop();
    LOGD("onErrorOccurred处理完成");
}