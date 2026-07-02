#include "downloadtask.h"
#include "downloadmanager.h" // 包含DownloadManager以获取线程池
#include "settingsmanager.h"
#include "logger.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QThreadPool>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>
#include <QNetworkProxy>
#include "historymanager.h"

/**
 * @brief 下载任务构造函数
 * 
 * 初始化下载任务对象，设置基本属性：
 * - URL：下载链接
 * - 保存路径：文件保存位置
 * - 线程数：下载线程数量
 * - 状态：初始为Pending
 * 
 * 提取文件名，初始化速度计算定时器（每秒计算一次下载速度）
 * 
 * @param url 下载文件的URL
 * @param savePath 文件保存的完整路径
 * @param threadCount 下载线程数量
 * @param parent 父对象指针
 */
DownloadTask::DownloadTask(const QUrl& url, const QString& savePath, int threadCount, QObject *parent)
    : QObject(parent),
      m_url(url),
      m_filePath(savePath),
      m_threadCount(threadCount),
      m_threadPool(DownloadManager::instance().threadPool()),
      m_status(DownloadTaskStatus::Pending),
      m_totalSize(0),
      m_downloadedSize(0),
      m_lastDownloadedSize(0),
      m_downloadSpeed(0),
      m_finishTime(), // 默认构造（无效 QDateTime），由完成/取消/失败路径显式设置
      m_headManager(nullptr),
      m_headReply(nullptr),
      m_finishedWorkers(0)
{
    // 防止 m_threadCount <= 0 导致后续除零；至少给1个线程
    if (m_threadCount <= 0) {
        LOGD(QString("构造时线程数异常(%1)，修正为1").arg(threadCount));
        m_threadCount = 1;
    }

    LOGD(QString("开始构造DownloadTask - URL:%1 保存路径:%2 线程数:%3").arg(url.toString()).arg(savePath).arg(m_threadCount));

    // 拉取启动时刻的代理设置；后续 SettingsManager 发出 settingsChanged 广播后
    // DownloadManager::onSettingsChanged 会调 applyProxy 更新本任务。
    {
        SettingsManager::ProxyType pt = SettingsManager::NoProxy;
        SettingsManager::instance().loadProxy(pt, m_proxy);
        m_proxy.setType(static_cast<QNetworkProxy::ProxyType>(pt));
        LOGD(QString("构造时已加载代理: 类型=%1 主机=%2").arg(int(m_proxy.type())).arg(m_proxy.hostName()));
    }

    // 简化构造函数，只做基本初始化
    QFileInfo fileInfo(m_filePath);
    m_fileName = fileInfo.fileName();
    LOGD(QString("提取文件名:%1").arg(m_fileName));
    
    // 获取系统临时目录
    m_tempDirectory = getTempDirectory();
    LOGD(QString("临时目录:%1").arg(m_tempDirectory));
    
    LOGD("DownloadTask构造完成");

    // 初始化速度计算定时器
    m_speedCalculationTimer.setInterval(1000); // 每秒计算一次速度
    connect(&m_speedCalculationTimer, &QTimer::timeout, this, &DownloadTask::onSpeedCalculationTimerTimeout);
    LOGD("速度计算定时器初始化完成");
}

DownloadTask::~DownloadTask()
{
    LOGD(QString("开始析构DownloadTask - 文件名:%1").arg(m_fileName));

    // 先断开所有信号连接，避免在清理过程中再有信号发出触发外部槽函数
    // 注意：不能用 blockSignals(true)，因为 setStatus 内部的 emit 是通过
    // QTimer::singleShot(0, ...) 异步排队，blockSignals 会把已排队的 emit 也屏蔽掉
    this->disconnect();

    // 停止所有worker
    LOGD(QString("停止所有worker，当前worker数量:%1").arg(m_workers.size()));
    for (HttpWorker* worker : m_workers) {
        if (worker) {
            worker->stop();
        }
    }

    // 等待线程池中的worker全部结束，避免析构时还有线程在跑
    LOGD("等待线程池中任务结束...");
    if (m_threadPool) {
        m_threadPool->waitForDone(3000);
    }

    // 清理网络资源
    if (m_headReply) {
        LOGD("中止HEAD请求");
        m_headReply->abort();
        m_headReply->deleteLater();
    }
    if (m_headManager) {
        LOGD("删除HEAD网络管理器");
        m_headManager->deleteLater();
    }

    // 使用deleteLater异步删除worker，避免阻塞
    LOGD("标记所有worker为延迟删除");
    for (HttpWorker* worker : m_workers) {
        if (worker) {
            worker->deleteLater();
        }
    }
    m_workers.clear();

    LOGD("DownloadTask析构完成");
}

/**
 * @brief 启动下载任务
 * 
 * 根据当前任务状态执行不同的启动逻辑：
 * 1. Pending/Failed状态：清理旧worker，重新初始化下载
 * 2. Paused状态：直接调用resume()恢复下载
 * 3. 其他状态：不执行任何操作
 * 
 * 使用异步调用避免阻塞主线程，通过QTimer延迟执行状态变更和初始化操作
 */
void DownloadTask::start()
{
    LOGD(QString("开始启动任务 - 当前状态:%1 URL:%2").arg(static_cast<int>(m_status)).arg(m_url.toString()));

    bool needInit = false;
    bool needResume = false;

    {
        QMutexLocker locker(&m_statusMutex); // 统一使用m_statusMutex保护m_status
        if (m_status == DownloadTaskStatus::Pending || m_status == DownloadTaskStatus::Failed) {
            LOGD("任务状态为Pending/Failed，清理旧worker并重新初始化下载");
            needInit = true;
        } else if (m_status == DownloadTaskStatus::Paused) {
            LOGD("任务状态为Paused，调用resume()");
            needResume = true;
        } else {
            LOGD(QString("任务状态不是Pending/Failed/Paused，不执行任何操作，状态:%1").arg(static_cast<int>(m_status)));
        }
    }

    if (needInit) {
        QList<HttpWorker*> oldWorkers;
        {
            QMutexLocker workerLocker(&m_mutex); // 保护m_workers
            LOGD(QString("当前workers数量: %1").arg(m_workers.size()));

            // 清理之前的worker
            for (HttpWorker* worker : m_workers) {
                if (worker) {
                    LOGD("停止并删除worker");
                    worker->stop();
                    worker->deleteLater();
                } else {
                    LOGD("发现空worker指针");
                }
            }
            m_workers.clear();
            m_finishedWorkers = 0;
            LOGD("worker清理完成");
        }

        m_startTime = QDateTime::currentDateTime();
        LOGD(QString("任务开始时间:%1").arg(m_startTime.toString()));

        // 使用QPointer安全包装this指针；将状态切换延迟到 lambda 内，
        // 保证 HEAD 请求真正发起后再把状态从 Pending 翻到 Downloading，
        // 避免出现"显示 Downloading 但 HEAD 还没发"的窗口
        QPointer<DownloadTask> safeThis(this);
        QTimer::singleShot(0, this, [safeThis]() {
            if (safeThis) {
                safeThis->setStatus(DownloadTaskStatus::Downloading); // 走setStatus，自动用m_statusMutex
                LOGD("异步执行initializeDownload()");
                safeThis->initializeDownload();
            }
        });
    } else if (needResume) {
        resume();
    }

    LOGD("任务启动完成");
}

/**
 * @brief 暂停下载任务
 * 
 * 当任务处于Downloading状态时执行暂停操作：
 * 1. 将状态设置为Paused
 * 2. 停止速度计算定时器
 * 3. 停止所有worker线程
 * 
 * 使用互斥锁保护状态和资源访问，确保线程安全
 */
void DownloadTask::pause()
{
    LOGD("开始暂停任务");
    QList<HttpWorker*> workersToStop;
    bool shouldStop = false;

    // 先获取worker锁
    {
        QMutexLocker workerLocker(&m_mutex);
        QMutexLocker statusLocker(&m_statusMutex);
        if (m_status == DownloadTaskStatus::Downloading) {
            LOGD("任务正在下载中，准备暂停");
            workersToStop = m_workers;
            shouldStop = true;
            m_speedCalculationTimer.stop();
            LOGD("已复制worker列表并停止定时器");
        }
    }

    // 在worker锁外处理状态变更和停止操作
    if (shouldStop) {
        setStatus(DownloadTaskStatus::Paused); // 内部使用statusMutex
        LOGD("任务状态设置为Paused");

        // 异步停止workers（在事件循环回到主线程时真正执行stop）
        for (HttpWorker* worker : workersToStop) {
            if (worker) {
                LOGD("异步停止worker");
                worker->stopAsync();
            }
        }
        LOGD(QString("任务暂停完成 - URL:%1").arg(url()));
    } else {
        LOGD(QString("任务不在下载状态，无法暂停"));
    }
}

/**
 * @brief 恢复下载任务
 * 
 * 当任务处于Paused状态时执行恢复操作：
 * 1. 将状态设置为Downloading
 * 2. 启动速度计算定时器
 * 3. 重新提交所有worker到线程池
 * 
 * 使用互斥锁保护状态和资源访问，确保线程安全
 */
void DownloadTask::resume()
{
    LOGD("开始恢复任务");
    QList<HttpWorker*> workersToResume;
    bool shouldResume = false;

    // 1. 获取worker列表
    {
        QMutexLocker workerLocker(&m_mutex);
        QMutexLocker statusLocker(&m_statusMutex);
        if (m_status == DownloadTaskStatus::Paused) {
            LOGD("任务已暂停，准备恢复");
            workersToResume = m_workers;
            shouldResume = true;
        }
    }

    // 2. 在锁外处理状态和worker提交
    if (shouldResume) {
        // 重置每个worker的运行状态。pause时worker.m_isStopped被置true，若不重置
        // worker.run()会直接return，导致断点续传失效
        LOGD(QString("重置%1个worker状态").arg(workersToResume.size()));
        for (HttpWorker* worker : workersToResume) {
            if (worker) {
                worker->reset();
            }
        }

        setStatus(DownloadTaskStatus::Downloading); // 使用statusMutex
        m_speedCalculationTimer.start();

        LOGD(QString("重新提交%1个worker到线程池").arg(workersToResume.size()));
        for (HttpWorker* worker : workersToResume) {
            m_threadPool->start(worker);
        }

        LOGD(QString("任务恢复完成 - URL:%1").arg(m_url.toString()));
    } else {
        LOGD(QString("任务不在暂停状态，无法恢复"));
    }
}

/**
 * @brief 取消下载任务
 * 
 * 取消当前下载任务，支持删除临时文件：
 * 1. 检查任务状态，已完成/已取消/失败的任务不能再次取消
 * 2. 设置状态为Cancelled，停止速度计算定时器
 * 3. 停止所有worker线程
 * 4. 根据参数决定是否删除临时文件
 * 5. 保存取消记录到历史
 * 6. 发射finished信号通知任务完成
 * 
 * @param deleteTempFiles 是否删除临时文件，true表示删除，false表示保留
 */
void DownloadTask::cancel(bool deleteTempFiles)
{
    LOGD(QString("开始取消任务，删除临时文件:%1").arg(deleteTempFiles));
    QList<HttpWorker*> workersToStop;
    bool shouldCancel = false;

    // 1. 检查状态并获取worker列表
    {
        QMutexLocker statusLocker(&m_statusMutex);
        QMutexLocker workerLocker(&m_mutex);
        if (m_status != DownloadTaskStatus::Completed &&
            m_status != DownloadTaskStatus::Cancelled &&
            m_status != DownloadTaskStatus::Failed) {
            workersToStop = m_workers;
            shouldCancel = true;
            m_speedCalculationTimer.stop();
            m_finishTime = QDateTime::currentDateTime();
        }
    }

    // 2. 在锁外执行取消操作
    if (shouldCancel) {
        // 停止所有worker
        LOGD(QString("停止%1个worker").arg(workersToStop.size()));
        for (HttpWorker* worker : workersToStop) {
            worker->stop();
        }

        // 状态变更
        setStatus(DownloadTaskStatus::Cancelled);
        LOGD(QString("任务状态设置为Cancelled，结束时间:%1").arg(m_finishTime.toString()));

        // 等待线程池中正在跑 run() 的 worker 真正退出，避免后续 deleteTempFiles 时
        // 仍有 worker 在写临时分片文件导致文件锁/句柄竞态
        LOGD("cancel: 等待线程池worker退出（超时3秒）");
        if (m_threadPool) {
            m_threadPool->waitForDone(3000);
        }

        // 耗时操作放在最后
        if (deleteTempFiles) {
            LOGD("删除临时文件");
            this->deleteTempFiles();
        }

        // 记录历史
        saveToHistory("Cancelled");
        if (!m_alreadyFinished) {
            m_alreadyFinished = true;
            emit finished();
        }
        LOGD(QString("任务取消完成 - URL:%1").arg(m_url.toString()));
    } else {
        LOGD("任务已完成或已取消，无需操作");
    }
}

/**
 * @brief 把新的代理设置同步到本任务持有的 QNAM 上。
 *
 * 由 DownloadManager::onSettingsChanged() 在 SettingsManager 发出
 * settingsChanged() 时调用。DownloadTask 与所有 HttpWorker 的 QNAM
 * 都在主线程构造（HttpWorker 通过 QTimer::singleShot(0, qApp, …) 在
 * 主线程 new QNetworkAccessManager），因此这里直接同步调用 setProxy 即可，
 * 不需要 QMetaObject::invokeMethod 跨线程派发。
 *
 * 行为：
 *   1. m_proxy 被更新——这是给后续 initializeDownload() / createHttpWorkers()
 *      新建 QNAM 时使用的最新代理值；
 *   2. m_headManager 已存在的话，立即同步 setProxy；
 *   3. 所有已存在 worker 的 QNAM 也立即 setProxy（已发出去的请求不会被打断，
 *      新的请求会带上新代理；Qt 的 QNetworkAccessManager 支持中途切换代理）。
 */
void DownloadTask::applyProxy(const QNetworkProxy& proxy)
{
    LOGD(QString("DownloadTask::applyProxy - 类型=%1 主机=%2")
         .arg(int(proxy.type())).arg(proxy.hostName()));

    m_proxy = proxy;

    // HEAD 请求的 QNAM（如果存在）：主线程同步设置
    if (m_headManager) {
        m_headManager->setProxy(proxy);
    }

    // 已存在的 worker：拷贝到本地列表再操作，避免持锁调外部接口
    QList<HttpWorker*> workerSnapshot;
    {
        QMutexLocker locker(&m_mutex);
        workerSnapshot = m_workers;
    }
    for (HttpWorker* worker : workerSnapshot) {
        if (worker) {
            worker->setProxy(proxy);
        }
    }
}

int DownloadTask::progressPercentage() const
{
    qint64 totalSize;
    qint64 downloadedSize;
    
    {
        QMutexLocker locker(&m_mutex); // 保护m_totalSize和m_downloadedSize
        totalSize = m_totalSize;
        downloadedSize = m_downloadedSize;
    }
    
    if (totalSize <= 0) {
        return 0;
    }
    int progress = static_cast<int>((static_cast<double>(downloadedSize) / totalSize) * 100);
    return progress;
}

void DownloadTask::setStatus(DownloadTaskStatus newStatus)
{
    bool shouldEmitSignal = false;
    DownloadTaskStatus statusToEmit;
    
    {
        QMutexLocker locker(&m_statusMutex); // 使用专门的状态锁
        if (m_status != newStatus) {
            LOGD(QString("任务状态变更：%1 -> %2").arg(static_cast<int>(m_status)).arg(static_cast<int>(newStatus)));
            m_status = newStatus;
            statusToEmit = newStatus;
            shouldEmitSignal = true;
        }
    }
    
    // 在互斥锁外异步发射信号，避免潜在的死锁
    if (shouldEmitSignal) {
        LOGD("准备异步发射statusChanged信号");
        QPointer<DownloadTask> safeThis(this);
        QTimer::singleShot(0, this, [safeThis, statusToEmit]() {
            if (safeThis) {
                LOGD("异步发射statusChanged信号");
                emit safeThis->statusChanged(statusToEmit);
            }
        });
        LOGD("statusChanged信号已排队发射");
    }
}

void DownloadTask::initializeDownload()
{
    LOGD("开始初始化下载...");

    if (m_status != DownloadTaskStatus::Downloading) {
        LOGD(QString("任务状态已变更，取消HEAD请求，当前状态:%1").arg(static_cast<int>(m_status)));
        return;
    }

    // 先验证目录
    QFileInfo fileInfo(m_filePath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        LOGD(QString("目录不存在，尝试创建目录:%1").arg(dir.path()));
        if (!dir.mkpath(".")) {
            LOGD(QString("无法创建下载目录:%1").arg(dir.path()));
            
            // 清理已创建的网络资源
            if (m_headReply) {
                m_headReply->deleteLater();
                m_headReply = nullptr;
            }
            if (m_headManager) {
                m_headManager->deleteLater();
                m_headManager = nullptr;
            }
            
            setStatus(DownloadTaskStatus::Failed);
            emit error(tr("无法创建下载目录: %1").arg(dir.path()));
            saveToHistory("Failed");
            if (!m_alreadyFinished) {
                m_alreadyFinished = true;
                emit finished();
            }
            return;
        }
        LOGD("目录创建成功");
    } else {
        LOGD("目录已存在");
    }

    // 文件系统操作不需要锁保护
    LOGD("创建HEAD请求的网络管理器");
    {
        QMutexLocker locker(&m_mutex); // 仅在创建网络管理器时加锁
        // 不要用 this 作为父对象，HEAD 请求通常早于 DownloadTask 析构；
        // 如果父对象先析构，QNetworkAccessManager 会跟着销毁，可能正在飞的 reply
        // 就会 UAF。改成无父对象，由 DownloadTask 显式管理 deleteLater 生命周期。
        m_headManager = new QNetworkAccessManager();
    }

    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(15000); // 15秒超时
    
    // 设置User-Agent，避免被网站屏蔽
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    
    // 设置其他常用头部
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9,zh-CN;q=0.8,zh;q=0.7");
    request.setRawHeader("Connection", "keep-alive");

    LOGD(QString("发送HEAD请求到:%1").arg(m_url.toString()));
    m_headReply = m_headManager->head(request);

    connect(m_headReply, &QNetworkReply::finished, this, &DownloadTask::onHeadRequestFinished);
    connect(m_headReply, &QNetworkReply::errorOccurred, this, &DownloadTask::onHeadRequestError);
    LOGD("HEAD请求信号连接完成");

    // 超时保护
    QPointer<QNetworkReply> safeHeadReply(m_headReply);
    QPointer<DownloadTask> safeThis(this);
    QTimer::singleShot(20000, safeThis, [safeThis, safeHeadReply]() {
        if (safeThis && safeHeadReply && !safeHeadReply->isFinished()) {
            LOGD("HEAD请求超时，强制中止");
            safeHeadReply->abort();
            // 标记超时状态避免后续处理（原子操作，防止多超时回调并发写）
            safeThis->m_headRequestTimedOut.storeRelease(1);

            // 超时后立即处理错误，避免等待其他回调；
            // 注意要排除 Cancelled / Completed 状态，避免覆盖 cancel() 后的状态
            QTimer::singleShot(100, safeThis, [safeThis]() {
                if (safeThis) {
                    QMutexLocker statusLocker(&safeThis->m_statusMutex);
                    if (safeThis->m_status == DownloadTaskStatus::Failed ||
                        safeThis->m_status == DownloadTaskStatus::Cancelled ||
                        safeThis->m_status == DownloadTaskStatus::Completed) {
                        return;
                    }
                }
                if (safeThis) {
                    LOGD("处理HEAD请求超时错误");
                    safeThis->handleHeadRequestError(tr("HEAD请求超时（20秒）"));
                }
            });
        }
    });

    LOGD("HEAD请求已发送，等待响应...");
}

void DownloadTask::handleHeadRequestError(const QString& errorString)
{
    LOGD(QString("处理HEAD请求错误:%1").arg(errorString));
    
    // 检查是否已经处理过错误（避免重复处理）
    if (m_status == DownloadTaskStatus::Failed) {
        LOGD("HEAD请求错误已处理，忽略重复处理");
        return;
    }
    
    // 清理网络资源
    if (m_headReply) {
        m_headReply->deleteLater();
        m_headReply = nullptr;
    }
    if (m_headManager) {
        m_headManager->deleteLater();
        m_headManager = nullptr;
    }
    
    emit error(tr("HEAD请求失败: %1").arg(errorString));
    setStatus(DownloadTaskStatus::Failed);
    saveToHistory("Failed");
    if (!m_alreadyFinished) {
        m_alreadyFinished = true;
        emit finished();
    }
}

void DownloadTask::processHeadResponse()
{
    // processHeadResponse 仅在主线程 onHeadRequestFinished 内被调用，
    // 与 m_totalSize / m_threadCount 的其他写入路径不并发，不需要 m_mutex。
    m_totalSize = m_headReply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    LOGD(QString("文件大小:%1 字节").arg(m_totalSize));

    if (m_totalSize <= 0) {
        LOGD("无法获取内容长度，强制使用单线程下载");
        m_threadCount = 1;
    }

    bool acceptRanges = m_headReply->rawHeader("Accept-Ranges") == "bytes";
    LOGD(QString("服务器支持Range请求:%1").arg(acceptRanges ? "是" : "否"));

    if (!acceptRanges && m_threadCount > 1) {
        LOGD("服务器不支持Range请求，强制使用单线程下载");
        m_threadCount = 1;
    }

    LOGD(QString("最终线程数:%1").arg(m_threadCount));
}

void DownloadTask::onHeadRequestFinished()
{
    LOGD("HEAD请求完成，检查响应...");

    // 用 QPointer 保护 this，避免在槽内访问已被 deleteLater 的对象
    QPointer<DownloadTask> safeThis(this);
    if (!safeThis) {
        return;
    }

    if (m_headRequestTimedOut.loadAcquire()) {
        LOGD("HEAD请求已超时，忽略此回调");
        return;
    }

    // 检查是否已经处理过错误（避免重复处理）
    if (m_status == DownloadTaskStatus::Failed) {
        LOGD("HEAD请求错误已处理，忽略此回调");
        return;
    }

    if (m_headReply->error() != QNetworkReply::NoError) {
        handleHeadRequestError(m_headReply->errorString());
        return;
    }

    LOGD("HEAD请求成功，开始解析响应头...");
    processHeadResponse();

    LOGD("清理HEAD请求资源");
    m_headReply->deleteLater();
    m_headManager->deleteLater();
    m_headReply = nullptr;
    m_headManager = nullptr;

    LOGD("开始创建HttpWorkers...");
    QTimer::singleShot(0, safeThis, [safeThis]() {
        if (safeThis) {
            LOGD("异步创建HttpWorkers...");
            safeThis->createHttpWorkers();
            safeThis->m_speedCalculationTimer.start();
            LOGD("HttpWorkers创建完成，速度计算定时器已启动");
        }
    });

    LOGD("HEAD请求处理完成");
}

void DownloadTask::onHeadRequestError(QNetworkReply::NetworkError code)
{
    Q_UNUSED(code);

    // 用 QPointer 保护 this，避免在槽内访问已被 deleteLater 的对象
    QPointer<DownloadTask> safeThis(this);
    if (!safeThis) {
        return;
    }
    
    // 检查请求是否已超时
    if (m_headRequestTimedOut.loadAcquire()) {
        LOGD("HEAD请求已超时，忽略此错误回调");
        return;
    }
    
    // 检查是否已经处理过错误（避免重复处理）
    if (m_status == DownloadTaskStatus::Failed) {
        LOGD("HEAD请求错误已处理，忽略此回调");
        return;
    }
    
    QString errorString;
    
    {
        QMutexLocker locker(&m_mutex); // 保护m_headReply
        if (m_headReply) {
            errorString = m_headReply->errorString();
        } else {
            errorString = tr("未知网络错误");
        }
    }
    
    LOGD(QString("HEAD请求错误，错误码:%1 错误信息:%2").arg(code).arg(errorString));
    
    // 清理网络资源
    if (m_headReply) {
        m_headReply->deleteLater();
        m_headReply = nullptr;
    }
    if (m_headManager) {
        m_headManager->deleteLater();
        m_headManager = nullptr;
    }
    
    emit error(tr("HEAD请求错误: %1").arg(errorString));
    setStatus(DownloadTaskStatus::Failed);
    saveToHistory("Failed");
    if (!m_alreadyFinished) {
        m_alreadyFinished = true;
        emit finished();
    }
}

void DownloadTask::createHttpWorkers()
{
    LOGD(QString("开始创建%1个HttpWorkers...").arg(m_threadCount));

    QMutexLocker locker(&m_mutex); // 保护m_workers

    // 防御性检查：m_threadCount 不能为 0，避免除零
    if (m_threadCount <= 0) {
        LOGD("线程数异常，强制为1");
        m_threadCount = 1;
    }

    // m_totalSize <= 0 时走单线程分支（不使用 Range）
    if (m_totalSize <= 0 || m_threadCount == 1) {
        // 单线程下载
        LOGD("使用单线程下载模式");
        QString tempFileName = QFileInfo(m_filePath).fileName() + ".part0";
        QString tempFilePath = QDir(m_tempDirectory).filePath(tempFileName);
        // 当 m_totalSize 未知时，endPoint 设为 -1 作为哨兵值，HttpWorker 检测到后
        // 不发送 Range 头，直接读完整文件直到服务器结束
        qint64 endPoint = (m_totalSize > 0) ? (m_totalSize - 1) : -1;
        LOGD(QString("创建单线程worker，临时文件:%1 范围:0-%2 (endPoint=-1表示整文件下载)")
             .arg(tempFilePath).arg(endPoint));
        HttpWorker* worker = new HttpWorker(m_url, tempFilePath, 0, endPoint);
        m_workers.append(worker);
        connect(worker, &HttpWorker::progress, this, &DownloadTask::onWorkerProgress);
        connect(worker, &HttpWorker::finished, this, &DownloadTask::onWorkerFinished);
        connect(worker, &HttpWorker::error, this, &DownloadTask::onWorkerError);
        LOGD("单线程worker创建完成，提交到线程池...");
        m_threadPool->start(worker);
        LOGD("单线程worker已提交到线程池");
        return;
    }

    LOGD(QString("使用多线程下载模式，文件大小:%1").arg(m_totalSize));

    // 把线程数限制在合理区间 [1, 16]。INT_MAX 会让 chunkSize 计算溢出
    // 或创建数千个 worker 把磁盘/线程池打爆。
    constexpr int kMaxThreadCount = 16;
    if (m_threadCount > kMaxThreadCount) {
        LOGD(QString("线程数(%1)超过上限%2，截断").arg(m_threadCount).arg(kMaxThreadCount));
        m_threadCount = kMaxThreadCount;
    }
    if (m_threadCount < 1) {
        LOGD(QString("线程数(%1)无效，修正为1").arg(m_threadCount));
        m_threadCount = 1;
    }

    // 防止 m_totalSize < m_threadCount 导致 chunkSize=0：
    // 实际并发数不超过文件总字节数（至少 1 字节/线程），多余的 worker 就不再创建
    const int effectiveThreadCount = qMin(m_threadCount, static_cast<int>(qMax<qint64>(1, m_totalSize)));
    if (effectiveThreadCount != m_threadCount) {
        LOGD(QString("线程数(%1)超过文件大小(%2)，调整为%3")
             .arg(m_threadCount).arg(m_totalSize).arg(effectiveThreadCount));
        m_threadCount = effectiveThreadCount;
    }

    const qint64 chunkSize = m_totalSize / m_threadCount;
    const qint64 leftover = m_totalSize % m_threadCount; // 余数字节，分散到前 leftover 个 worker
    QString baseFileName = QFileInfo(m_filePath).fileName();

    for (int i = 0; i < m_threadCount; ++i) {
        // 把余数字节按 1 byte/worker 均匀分散给前 leftover 个 worker
        const qint64 extra = (i < leftover) ? 1 : 0;
        const qint64 startPoint = i * chunkSize + extra;
        qint64 endPoint = (i == m_threadCount - 1) ? (m_totalSize - 1) : (startPoint + chunkSize - 1);
        if (endPoint >= m_totalSize) {
            endPoint = m_totalSize - 1;
        }
        QString tempFileName = baseFileName + QString(".part%1").arg(i);
        QString tempFilePath = QDir(m_tempDirectory).filePath(tempFileName);

        LOGD(QString("创建worker%1 范围:%2-%3 临时文件:%4").arg(i).arg(startPoint).arg(endPoint).arg(tempFilePath));

        HttpWorker* worker = new HttpWorker(m_url, tempFilePath, startPoint, endPoint);
        m_workers.append(worker);

        connect(worker, &HttpWorker::progress, this, &DownloadTask::onWorkerProgress);
        connect(worker, &HttpWorker::finished, this, &DownloadTask::onWorkerFinished);
        connect(worker, &HttpWorker::error, this, &DownloadTask::onWorkerError);

        LOGD(QString("worker%1创建完成，提交到线程池...").arg(i));
        m_threadPool->start(worker);
        LOGD(QString("worker%1已提交到线程池").arg(i));
    }

    LOGD("所有workers创建完成");
}

void DownloadTask::onWorkerProgress(qint64 bytes)
{
    // 防止合并/取消/失败之后还有迟到 onWorkerProgress 信号
    // 把 m_downloadedSize 推回小于 totalSize 的值，进而误导 UI 显示未完成
    {
        QMutexLocker statusLocker(&m_statusMutex);
        if (m_status == DownloadTaskStatus::Completed ||
            m_status == DownloadTaskStatus::Failed ||
            m_status == DownloadTaskStatus::Cancelled) {
            return;
        }
    }

    qint64 downloadedSize;
    qint64 totalSize;
    qint64 downloadSpeed;

    {
        QMutexLocker locker(&m_mutex); // 保护m_downloadedSize和m_lastDownloadedSize
        m_downloadedSize += bytes;

        // Copy values to local variables before releasing the mutex
        downloadedSize = m_downloadedSize;
        totalSize = m_totalSize;
        downloadSpeed = m_downloadSpeed;
    }

    // Emit signal outside of mutex lock to avoid potential deadlocks
    emit progressUpdated(downloadedSize, totalSize, downloadSpeed);
}

void DownloadTask::onWorkerFinished()
{
    bool shouldMergeFiles = false;
    int finishedCount = 0;

    {
        QMutexLocker locker(&m_mutex); // 保护m_finishedWorkers和m_workers
        m_finishedWorkers++;
        finishedCount = m_finishedWorkers;

        // 直接比较，不调用allWorkersFinished()方法
        shouldMergeFiles = (m_finishedWorkers == m_threadCount);
    }

    LOGD(QString("worker完成，已完成worker数:%1/%2").arg(finishedCount).arg(m_threadCount));

    if (shouldMergeFiles) {
        LOGD("所有worker完成，先停止所有worker确保它们不再写文件");
        // 在合并/删除临时文件前，先确保所有worker都已停止（stopAsync内部已经
        // 在onFinished中调用过cleanup，但保险起见再发一次）
        {
            QList<HttpWorker*> workers;
            {
                QMutexLocker locker(&m_mutex);
                workers = m_workers;
            }
            for (HttpWorker* worker : workers) {
                if (worker) {
                    worker->stopAsync();
                }
            }
        }

        LOGD("开始合并文件");
        m_speedCalculationTimer.stop();
        if (mergeFiles()) {
            LOGD("文件合并成功");
            setStatus(DownloadTaskStatus::Completed);
            m_finishTime = QDateTime::currentDateTime();
            saveToHistory("Completed");
            if (!m_alreadyFinished) {
                m_alreadyFinished = true;
                emit finished();
            }
            LOGD(QString("任务完成 - URL:%1").arg(m_url.toString()));
        } else {
            LOGD("文件合并失败");
            setStatus(DownloadTaskStatus::Failed);
            m_finishTime = QDateTime::currentDateTime();
            saveToHistory("Failed");
            emit error(tr("文件合并失败！"));
            if (!m_alreadyFinished) {
                m_alreadyFinished = true;
                emit finished();
            }
            LOGD(QString("任务失败 - URL:%1").arg(m_url.toString()));
        }
        // 临时文件删除由mergeFiles()内部完成，避免worker还在写时被unlink
    }
}

void DownloadTask::onWorkerError(const QString& errorString)
{
    bool shouldStopWorkers = false;

    {
        QMutexLocker statusLocker(&m_statusMutex); // 统一用m_statusMutex保护m_status
        if (m_status != DownloadTaskStatus::Failed && m_status != DownloadTaskStatus::Cancelled) {
            LOGD(QString("worker出错，错误信息:%1").arg(errorString));
            // 走 setStatus() 以确保 statusChanged 信号被发射
            setStatus(DownloadTaskStatus::Failed);
            m_speedCalculationTimer.stop();
            m_finishTime = QDateTime::currentDateTime();
            saveToHistory("Failed");
            shouldStopWorkers = true;
        }
    }

    if (shouldStopWorkers) {
        QList<HttpWorker*> workers;
        {
            QMutexLocker locker(&m_mutex);
            workers = m_workers;
        }
        LOGD(QString("停止所有其他worker，总数:%1").arg(workers.size()));
        // 停止所有其他worker
        for (HttpWorker* worker : workers) {
            if (worker) {
                worker->stop();
            }
        }

        // 发射信号
        emit error(tr("下载任务出错: %1").arg(errorString));
        if (!m_alreadyFinished) {
            m_alreadyFinished = true;
            emit finished(); // 通知管理器任务已完成（失败也是一种完成状态）
        }
        LOGD(QString("任务错误处理完成 - URL:%1 错误:%2").arg(m_url.toString()).arg(errorString));
    }
}

void DownloadTask::onSpeedCalculationTimerTimeout()
{
    qint64 downloadedSize;
    qint64 downloadSpeed;

    {
        QMutexLocker locker(&m_mutex); // 保护m_downloadedSize和m_lastDownloadedSize
        // 合并完成后 m_downloadedSize 可能被回写为 totalSize，
        // 再减去旧值会得到负数；clamp 到 0 避免 UI 显示负速度。
        const qint64 delta = m_downloadedSize - m_lastDownloadedSize;
        m_downloadSpeed = delta > 0 ? delta : 0;
        m_lastDownloadedSize = m_downloadedSize;

        // Copy values to local variables before releasing the mutex
        downloadedSize = m_downloadedSize;
        downloadSpeed = m_downloadSpeed;
    }

    // Emit signal outside of mutex lock to avoid potential deadlocks
    emit progressUpdated(downloadedSize, m_totalSize, downloadSpeed);
}

bool DownloadTask::allWorkersFinished() const
{
    int finishedWorkers;
    int threadCount;
    
    {
        QMutexLocker locker(&m_mutex); // 保护m_finishedWorkers和m_threadCount
        finishedWorkers = m_finishedWorkers;
        threadCount = m_threadCount;
    }
    
    return finishedWorkers == threadCount;
}

bool DownloadTask::prepareFinalFile(QFile& finalFile)
{
    QFileInfo fileInfo(m_filePath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        LOGD(QString("目标目录不存在，创建目录:%1").arg(dir.path()));
        if (!dir.mkpath(".")) {
            LOGD(QString("无法创建最终文件目录:%1").arg(dir.path()));
            return false;
        }
    }

    if (!finalFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOGD(QString("无法打开最终文件进行写入:%1 错误:%2").arg(m_filePath).arg(finalFile.errorString()));
        return false;
    }
    return true;
}

bool DownloadTask::mergeTempFile(const QString& tempFilePath, QFile& finalFile, qint64& totalBytesWritten)
{
    QFile tempFile(tempFilePath);
    if (!tempFile.exists()) {
        LOGD(QString("临时文件不存在，合并失败:%1").arg(tempFilePath));
        return false;
    }

    qint64 tempFileSize = tempFile.size();
    if (!tempFile.open(QIODevice::ReadOnly)) {
        LOGD(QString("无法打开临时文件进行读取:%1 错误:%2").arg(tempFilePath).arg(tempFile.errorString()));
        return false;
    }

    QByteArray buffer;
    qint64 partBytesWritten = 0;
    while (!tempFile.atEnd()) {
        buffer = tempFile.read(1024 * 1024); // 1MB buffer
        qint64 bytesRead = buffer.size();
        if (bytesRead == 0) break;

        qint64 bytesWritten = finalFile.write(buffer);
        if (bytesWritten != bytesRead) {
            LOGD(QString("写入最终文件失败，期望:%1 实际:%2").arg(bytesRead).arg(bytesWritten));
            tempFile.close();
            return false;
        }
        totalBytesWritten += bytesWritten;
        partBytesWritten += bytesWritten;
    }
    tempFile.close();
    LOGD(QString("临时文件%1合并完成，写入字节数:%2").arg(QFileInfo(tempFilePath).fileName()).arg(partBytesWritten));
    return true;
}

bool DownloadTask::validateFinalFile(qint64 totalBytesWritten, qint64 expectedSize)
{
    QFileInfo finalFileInfo(m_filePath);
    if (finalFileInfo.size() != expectedSize) {
        LOGD(QString("最终文件大小与期望不符，实际:%1 期望:%2").arg(finalFileInfo.size()).arg(expectedSize));
        QFile::remove(m_filePath);
        return false;
    }

    if (finalFileInfo.size() != totalBytesWritten) {
        LOGD(QString("最终文件大小与写入字节数不符，实际:%1 写入:%2").arg(finalFileInfo.size()).arg(totalBytesWritten));
        QFile::remove(m_filePath);
        return false;
    }

    LOGD(QString("最终文件大小验证通过:%1字节").arg(finalFileInfo.size()));
    return true;
}

bool DownloadTask::mergeFiles()
{
    LOGD("开始合并文件");

    // 在临时目录中创建临时合并文件
    QString tempMergeFileName = QFileInfo(m_filePath).fileName() + ".merge";
    QString tempMergeFilePath = QDir(m_tempDirectory).filePath(tempMergeFileName);

    QFile tempMergeFile(tempMergeFilePath);
    if (!tempMergeFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOGD(QString("无法创建临时合并文件:%1 错误:%2").arg(tempMergeFilePath).arg(tempMergeFile.errorString()));
        return false;
    }

    qint64 totalBytesWritten = 0;
    qint64 totalTempFileSize = 0;
    int threadCount = getThreadCount();

    LOGD(QString("开始合并%1个临时文件到临时合并文件:%2").arg(threadCount).arg(tempMergeFilePath));

    for (int i = 0; i < threadCount; ++i) {
        QString tempFileName = QFileInfo(m_filePath).fileName() + QString(".part%1").arg(i);
        QString tempFilePath = QDir(m_tempDirectory).filePath(tempFileName);
        if (!mergeTempFile(tempFilePath, tempMergeFile, totalBytesWritten)) {
            tempMergeFile.close();
            QFile::remove(tempMergeFilePath); // 清理临时合并文件
            return false;
        }
        totalTempFileSize += QFileInfo(tempFilePath).size();
    }

    tempMergeFile.close();

    qint64 totalSize = getTotalSize();
    LOGD(QString("文件合并完成，总写入字节数:%1 临时文件总大小:%2 期望总大小:%3").arg(totalBytesWritten).arg(totalTempFileSize).arg(totalSize));

    // 验证临时合并文件的大小
    if (totalBytesWritten != totalSize) {
        LOGD(QString("临时合并文件大小与期望不符，实际:%1 期望:%2").arg(totalBytesWritten).arg(totalSize));
        QFile::remove(tempMergeFilePath);
        return false;
    }

    // 将临时合并文件移动到最终位置
    if (!moveFileToFinalLocation(tempMergeFilePath, m_filePath)) {
        LOGD(QString("无法将临时合并文件移动到最终位置:%1 -> %2").arg(tempMergeFilePath).arg(m_filePath));
        QFile::remove(tempMergeFilePath);
        return false;
    }

    // 验证最终文件
    if (!validateFinalFile(totalBytesWritten, totalSize)) {
        return false;
    }

    updateDownloadedSize(totalSize);
    LOGD(QString("文件成功移动到最终位置:%1").arg(m_filePath));

    // 合并完成且最终文件已落盘后再删除临时分片文件，确保worker不再写时再unlink
    LOGD("合并完成，开始删除临时分片文件");
    deleteTempFiles();
    return true;
}

void DownloadTask::deleteTempFiles()
{
    int threadCount;
    
    {
        QMutexLocker locker(&m_mutex); // 保护m_threadCount
        threadCount = m_threadCount;
    }
    
    LOGD(QString("开始删除%1个临时文件").arg(threadCount));
    
    QString baseFileName = QFileInfo(m_filePath).fileName();
    
    for (int i = 0; i < threadCount; ++i) {
        QString tempFileName = baseFileName + QString(".part%1").arg(i);
        QString tempFilePath = QDir(m_tempDirectory).filePath(tempFileName);
        bool removed = QFile::remove(tempFilePath);
        LOGD(QString("删除临时文件%1:%2 结果:%3").arg(i).arg(tempFilePath).arg(removed ? "成功" : "失败"));
    }
    
    // 也尝试删除临时合并文件（如果存在）
    QString tempMergeFileName = baseFileName + ".merge";
    QString tempMergeFilePath = QDir(m_tempDirectory).filePath(tempMergeFileName);
    if (QFile::exists(tempMergeFilePath)) {
        bool removed = QFile::remove(tempMergeFilePath);
        LOGD(QString("删除临时合并文件:%1 结果:%2").arg(tempMergeFilePath).arg(removed ? "成功" : "失败"));
    }
    
    LOGD("临时文件删除完成");
}

int DownloadTask::getThreadCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_threadCount;
}

qint64 DownloadTask::getTotalSize() const
{
    QMutexLocker locker(&m_mutex);
    return m_totalSize;
}

void DownloadTask::updateDownloadedSize(qint64 size)
{
    QMutexLocker locker(&m_mutex);
    m_downloadedSize = size;
}

void DownloadTask::saveToHistory(const QString& status)
{
    LOGD(QString("开始保存任务历史记录，状态:%1").arg(status));
    
    // 准备记录数据（不需要锁）
    DownloadRecord record;
    
    // 使用专用锁快速拷贝数据
    {
        QMutexLocker locker(&m_historyMutex);
        record.url = m_url.toString();
        record.filePath = m_filePath;
        record.fileSize = m_totalSize;
        record.startTime = m_startTime;
        record.finishTime = m_finishTime;
        record.fileName = m_fileName;
    }
    
    record.status = status;
    
    // 在锁外执行数据库操作
    QTimer::singleShot(0, this, [record]() {
        HistoryManager::instance().addRecord(record);
    });
    
    LOGD("历史记录保存请求已提交");
}

QString DownloadTask::getTempDirectory() const
{
    // 优先使用环境变量中的临时目录
    QString tempPath = QString::fromLocal8Bit(qgetenv("TEMP"));
    if (tempPath.isEmpty()) {
        tempPath = QString::fromLocal8Bit(qgetenv("TMP"));
    }
    
    // 如果环境变量没有设置，使用Qt的标准临时目录
    if (tempPath.isEmpty()) {
        tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    
    // 确保目录存在
    QDir tempDir(tempPath);
    if (!tempDir.exists()) {
        tempDir.mkpath(".");
    }
    
    LOGD(QString("获取临时目录:%1").arg(tempPath));
    return tempPath;
}

bool DownloadTask::moveFileToFinalLocation(const QString& tempFilePath, const QString& finalFilePath)
{
    LOGD(QString("开始移动文件从临时目录到最终位置: %1 -> %2").arg(tempFilePath).arg(finalFilePath));
    
    // 确保目标目录存在
    QFileInfo finalFileInfo(finalFilePath);
    QDir targetDir = finalFileInfo.dir();
    if (!targetDir.exists()) {
        if (!targetDir.mkpath(".")) {
            LOGD(QString("无法创建目标目录:%1").arg(targetDir.path()));
            return false;
        }
    }
    
    // 如果目标文件已存在，先删除它（TOCTOU：直接 remove 即可，存在则删、不存在则静默 no-op）
    if (!QFile::remove(finalFilePath)) {
        // 如果文件不存在，QFile::remove 返回 false 但不视为错误；区分"不存在"和"删除失败"
        QFileInfo fi(finalFilePath);
        if (fi.exists()) {
            LOGD(QString("无法删除已存在的目标文件:%1").arg(finalFilePath));
            return false;
        }
    }
    
    // 尝试重命名（移动）文件
    if (QFile::rename(tempFilePath, finalFilePath)) {
        LOGD(QString("文件移动成功:%1 -> %2").arg(tempFilePath).arg(finalFilePath));
        return true;
    }
    
    // 如果重命名失败，尝试复制然后删除
    LOGD(QString("重命名失败，尝试复制文件:%1 -> %2").arg(tempFilePath).arg(finalFilePath));
    if (QFile::copy(tempFilePath, finalFilePath)) {
        if (QFile::remove(tempFilePath)) {
            LOGD(QString("文件复制并删除成功:%1 -> %2").arg(tempFilePath).arg(finalFilePath));
            return true;
        } else {
            LOGD(QString("文件复制成功但删除临时文件失败:%1").arg(tempFilePath));
            // 复制成功但删除失败，也算成功
            return true;
        }
    }
    
    LOGD(QString("文件移动失败:%1 -> %2").arg(tempFilePath).arg(finalFilePath));
    return false;
}