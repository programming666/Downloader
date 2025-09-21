#include "httpworker.h"
#include "logger.h"
#include <QTimer>
#include <QThread>
#include <QApplication>

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
      m_isStopped(false)
{
    LOGD(QString("构造HttpWorker - URL:%1 文件路径:%2 范围:%3-%4").arg(url.toString()).arg(filePath).arg(startPoint).arg(endPoint));
    setAutoDelete(false); // 不自动删除，由DownloadTask管理
    LOGD("HttpWorker构造完成，设置为手动删除模式");
}

HttpWorker::~HttpWorker()
{
    LOGD(QString("开始析构HttpWorker - 文件:%1").arg(m_filePath));
    
    // 确保所有资源都被清理
    if (m_reply) {
        LOGD("中止网络请求");
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file && m_file->isOpen()) {
        LOGD("关闭文件");
        m_file->close();
    }
    if (m_file) {
        LOGD("删除文件对象");
        delete m_file;
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
    LOGD(QString("当前线程:%1 主线程:%2").arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16).arg(reinterpret_cast<quintptr>(qApp->thread()), 0, 16));
    
    if (m_isStopped) {
        LOGD("任务已停止，直接退出run方法");
        return;
    }

    LOGD("开始调用startDownload()");
    startDownload();
    LOGD("startDownload()调用完成");
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
 */
void HttpWorker::startDownload()
{
    LOGD(QString("开始网络下载，范围:%1-%2").arg(m_startPoint).arg(m_endPoint));
    
    if (m_isStopped) {
        LOGD("任务已停止，退出startDownload");
        cleanup();
        return;
    }
    
    LOGD("开始创建QNetworkAccessManager...");

    // 检查当前线程
    LOGD(QString("当前线程:%1 主线程:%2").arg(reinterpret_cast<quintptr>(QThread::currentThread()), 0, 16).arg(reinterpret_cast<quintptr>(qApp->thread()), 0, 16));
    
    // 如果已经在主线程，直接创建网络管理器
    if (QThread::currentThread() == qApp->thread()) {
        LOGD("已经在主线程中，直接创建QNetworkAccessManager");
        if (m_isStopped) {
            LOGD("任务已停止，取消网络管理器创建");
            cleanup();
            return;
        }
        m_netManager = new QNetworkAccessManager();
        LOGD("QNetworkAccessManager创建完成，开始网络请求");
        continueDownload();
    } else {
        // 使用QTimer::singleShot在主线程中创建网络管理器
        QTimer::singleShot(0, qApp, [this]() {
            if (m_isStopped) {
                LOGD("任务已停止，取消网络管理器创建");
                cleanup();
                return;
            }
            
            LOGD("在主线程中创建QNetworkAccessManager");
            m_netManager = new QNetworkAccessManager();
            LOGD("QNetworkAccessManager创建完成，开始网络请求");
            
            // 继续执行下载逻辑
            continueDownload();
        });
    }
}

void HttpWorker::continueDownload()
{

    LOGD(QString("开始创建文件对象:%1").arg(m_filePath));
    m_file = new QFile(m_filePath);
    LOGD("文件对象创建完成");

    // 检查是否需要断点续传
    LOGD(QString("检查文件是否存在:%1").arg(m_file->exists() ? "存在" : "不存在"));
    if (m_file->exists()) {
        m_bytesReceived = m_file->size();
        LOGD(QString("文件已存在，大小:%1，使用追加模式").arg(m_bytesReceived));
        if (!m_file->open(QIODevice::Append)) {
            LOGD(QString("无法打开文件进行追加，错误:%1").arg(m_file->errorString()));
            emit error(tr("无法打开临时文件进行追加: %1").arg(m_file->errorString()));
            cleanup();
            return;
        }
        LOGD("文件以追加模式打开成功");
    } else {
        LOGD("文件不存在，创建新文件");
        if (!m_file->open(QIODevice::WriteOnly)) {
            LOGD(QString("无法创建文件，错误:%1").arg(m_file->errorString()));
            emit error(tr("无法创建临时文件: %1").arg(m_file->errorString()));
            cleanup();
            return;
        }
        LOGD("新文件创建成功");
    }

    qint64 currentStartPoint = m_startPoint + m_bytesReceived;
    LOGD(QString("计算当前开始点:%1 (原始开始点:%2 + 已接收字节数:%3)").arg(currentStartPoint).arg(m_startPoint).arg(m_bytesReceived));

    // 如果这个分块已经下载完成
    if (currentStartPoint > m_endPoint) {
        LOGD(QString("分块已完成下载，当前开始点:%1 > 结束点:%2").arg(currentStartPoint).arg(m_endPoint));
        m_file->close();
        cleanup();
        emit finished();
        return;
    }

    QNetworkRequest request(m_url);
    // 设置Range头，请求文件的特定部分
    QString rangeHeader = QString("bytes=%1-%2").arg(currentStartPoint).arg(m_endPoint);
    request.setRawHeader("Range", rangeHeader.toUtf8());
    request.setTransferTimeout(30000); // 30秒超时
    
    // 设置User-Agent，避免被网站屏蔽
    request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    
    // 设置其他常用头部
    request.setRawHeader("Accept", "*/*");
    request.setRawHeader("Accept-Language", "en-US,en;q=0.9,zh-CN;q=0.8,zh;q=0.7");
    request.setRawHeader("Connection", "keep-alive");
    
    LOGD(QString("设置Range头:%1").arg(rangeHeader));

    LOGD("发送网络请求...");
    LOGD("开始调用m_netManager->get()...");
    m_reply = m_netManager->get(request);
    LOGD("m_netManager->get()调用完成，开始连接信号...");

    connect(m_reply, &QNetworkReply::readyRead, this, &HttpWorker::onReadyRead);
    LOGD("readyRead信号连接完成");
    connect(m_reply, &QNetworkReply::finished, this, &HttpWorker::onFinished);
    LOGD("finished信号连接完成");
    connect(m_reply, &QNetworkReply::errorOccurred, this, &HttpWorker::onErrorOccurred);
    LOGD("errorOccurred信号连接完成");
    
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
    if (QThread::currentThread() == this->thread()) {
        stop();
    } else {
        QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection);
    }
}

void HttpWorker::stop()
{
    LOGD("停止HttpWorker");
    m_isStopped = true;
    
    // 立即关闭文件（线程安全方式）
    if (m_file && m_file->isOpen()) {
        LOGD("立即关闭文件");
        m_file->close();
    }

    // 检查当前线程是否为主线程
    if (QThread::currentThread() == qApp->thread()) {
        LOGD("在主线程中执行停止操作");
        if (m_reply) {
            if (m_reply->isRunning()) {
                LOGD("直接中止网络请求并清空缓冲区");
                m_reply->abort();
                m_reply->readAll(); // 清空接收缓冲区
            }
            m_reply->deleteLater();
            m_reply = nullptr;
        }
    } else {
        LOGD("在非主线程中执行停止操作，使用线程安全方式");
        QMetaObject::invokeMethod(this, [this]() {
            if (m_reply) {
                if (m_reply->isRunning()) {
                    LOGD("通过主线程中止网络请求并清空缓冲区");
                    m_reply->abort();
                    m_reply->readAll(); // 清空接收缓冲区
                }
                m_reply->deleteLater();
                m_reply = nullptr;
            }
        }, Qt::QueuedConnection);
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
        m_bytesReceived += dataSize;
        emit progress(dataSize);
        
        // 每接收1MB数据记录一次日志，避免日志过多
        static qint64 lastLoggedBytes = 0;
        if (m_bytesReceived - lastLoggedBytes >= 1024 * 1024) {
            LOGD(QString("接收数据进度 - 本次:%1字节 总计:%2字节").arg(dataSize).arg(m_bytesReceived));
            lastLoggedBytes = m_bytesReceived;
        }
    }
}

void HttpWorker::onFinished()
{
    LOGD("网络请求完成，开始处理结果...");
    
    if (m_isStopped) {
        LOGD("任务已停止，清理资源并退出");
        cleanup();
        return;
    }

    if (m_reply->error() == QNetworkReply::NoError) {
        LOGD(QString("下载成功完成 - 总接收字节数:%1").arg(m_bytesReceived));
        emit finished();
    } else {
        LOGD(QString("下载出现错误 - 错误码:%1 错误信息:%2").arg(m_reply->error()).arg(m_reply->errorString()));
        emit error(m_reply->errorString());
    }
    
    LOGD("开始清理资源");
    cleanup();
    LOGD("onFinished处理完成");
}

void HttpWorker::onErrorOccurred(QNetworkReply::NetworkError code)
{
    LOGD(QString("网络错误发生 - 错误码:%1").arg(code));
    
    QString errorString = m_reply ? m_reply->errorString() : "未知错误";
    LOGD(QString("错误详细信息:%1").arg(errorString));
    
    if (m_isStopped && code == QNetworkReply::OperationCanceledError) {
        // 用户主动停止，不是错误
        LOGD("用户主动停止下载，这不是错误");
        cleanup();
        emit finished();
        return;
    }
    
    // 检查是否需要重试（网络相关错误）
    static int retryCount = 0;
    const int maxRetries = 3;
    
    if (retryCount < maxRetries && 
        (code == QNetworkReply::ConnectionRefusedError ||
         code == QNetworkReply::RemoteHostClosedError ||
         code == QNetworkReply::TimeoutError ||
         code == QNetworkReply::TemporaryNetworkFailureError)) {
        
        retryCount++;
        LOGD(QString("网络错误，尝试第%1次重试...").arg(retryCount));
        
        // 清理当前资源
        if (m_reply) {
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        
        // 延迟重试
        QTimer::singleShot(2000 * retryCount, this, [this]() {
            if (!m_isStopped) {
                LOGD("执行重试下载");
                startDownload();
            }
        });
        
        return;
    }
    
    // 重置重试计数器
    retryCount = 0;
    
    LOGD("发射错误信号并清理资源");
    emit error(errorString);
    cleanup();
    LOGD("onErrorOccurred处理完成");
}