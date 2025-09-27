#include "downloadtask.h"
#include "downloadmanager.h" // 包含DownloadManager以获取线程池
#include "logger.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QThreadPool>
#include <QMetaObject>
#include <QPointer>
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
      m_status(DownloadTaskStatus::Pending),
      m_totalSize(0),
      m_downloadedSize(0),
      m_lastDownloadedSize(0),
      m_downloadSpeed(0),
      m_headManager(nullptr),
      m_headReply(nullptr),
      m_finishedWorkers(0)
{
    LOGD(QString("开始构造DownloadTask - URL:%1 保存路径:%2 线程数:%3").arg(url.toString()).arg(savePath).arg(threadCount));
    
    // 简化构造函数，只做基本初始化
    QFileInfo fileInfo(m_filePath);
    m_fileName = fileInfo.fileName();
    LOGD(QString("提取文件名:%1").arg(m_fileName));
    
    LOGD("DownloadTask构造完成");

    // 初始化速度计算定时器
    m_speedCalculationTimer.setInterval(1000); // 每秒计算一次速度
    connect(&m_speedCalculationTimer, &QTimer::timeout, this, &DownloadTask::onSpeedCalculationTimerTimeout);
    LOGD("速度计算定时器初始化完成");
}

DownloadTask::~DownloadTask()
{
    LOGD(QString("开始析构DownloadTask - 文件名:%1").arg(m_fileName));
    
    // 停止所有worker
    LOGD(QString("停止所有worker，当前worker数量:%1").arg(m_workers.size()));
    for (HttpWorker* worker : m_workers) {
        worker->stop();
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
        worker->deleteLater();
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

    QMutexLocker locker(&m_mutex); // 保护m_status和m_workers
    if (m_status == DownloadTaskStatus::Pending || m_status == DownloadTaskStatus::Failed) {
        LOGD("任务状态为Pending/Failed，清理旧worker并重新初始化下载");
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
        
        // 直接设置状态，避免在已有锁的情况下再次加锁
        LOGD(QString("任务状态变更：%1 -> %2").arg(static_cast<int>(m_status)).arg(static_cast<int>(DownloadTaskStatus::Downloading)));
        m_status = DownloadTaskStatus::Downloading;
        m_startTime = QDateTime::currentDateTime();
        LOGD(QString("任务状态设置为Downloading，开始时间:%1").arg(m_startTime.toString()));
        
        // 使用异步调用避免阻塞主线程
        locker.unlock();
        
        // 使用QPointer安全包装this指针
        QPointer<DownloadTask> safeThis(this);
        
        // 异步发射状态变更信号
        QTimer::singleShot(0, this, [safeThis]() {
            if (safeThis) {
                LOGD("异步发射statusChanged信号");
                emit safeThis->statusChanged(DownloadTaskStatus::Downloading);
            }
        });
        LOGD("开始异步调用initializeDownload()");
        QTimer::singleShot(0, this, [safeThis]() {
            if (safeThis) {
                LOGD("异步执行initializeDownload()");
                safeThis->initializeDownload();
            }
        });
    } else if (m_status == DownloadTaskStatus::Paused) {
        LOGD("任务状态为Paused，调用resume()");
        locker.unlock();
        resume();
    } else {
        LOGD(QString("任务状态不是Pending/Failed/Paused，不执行任何操作，状态:%1").arg(static_cast<int>(m_status)));
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
        
        // 异步停止workers
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
    LOGD("暂停操作完成，开始停止workers");
    // 在互斥锁外直接停止workers，避免事件循环依赖
    if (shouldStop) {
        LOGD("开始停止workers");
        
        // 使用异步停止避免在主线程中阻塞
        for (HttpWorker* worker : workersToStop) {
            if (worker) {
                LOGD("异步停止worker");
                worker->stopAsync(); // 使用异步停止方法
            }
        }
        
        LOGD(QString("任务暂停完成 - URL:%1").arg(url()));
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
        if (m_status == DownloadTaskStatus::Paused) {
            LOGD("任务已暂停，准备恢复");
            workersToResume = m_workers;
            shouldResume = true;
        }
    }
    
    // 2. 在锁外处理状态和worker提交
    if (shouldResume) {
        setStatus(DownloadTaskStatus::Downloading); // 使用statusMutex
        m_speedCalculationTimer.start();
        
        LOGD(QString("重新提交%1个worker到线程池").arg(workersToResume.size()));
        for (HttpWorker* worker : workersToResume) {
            QThreadPool::globalInstance()->start(worker);
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
        QMutexLocker locker(&m_mutex);
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
        
        // 耗时操作放在最后
        if (deleteTempFiles) {
            LOGD("删除临时文件");
            this->deleteTempFiles();
        }
        
        // 记录历史
        saveToHistory("Cancelled");
        emit finished();
        LOGD(QString("任务取消完成 - URL:%1").arg(m_url.toString()));
    } else {
        LOGD("任务已完成或已取消，无需操作");
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
            emit finished();
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
        m_headManager = new QNetworkAccessManager(this); // 设置父对象保证在主线程
    }

    QNetworkRequest request(m_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
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
    QTimer::singleShot(20000, this, [safeThis, safeHeadReply]() {
        if (safeThis && safeHeadReply && !safeHeadReply->isFinished()) {
            LOGD("HEAD请求超时，强制中止");
            safeHeadReply->abort();
            // 标记超时状态避免后续处理
            safeThis->m_headRequestTimedOut = true;
            
            // 超时后立即处理错误，避免等待其他回调
            QTimer::singleShot(100, safeThis, [safeThis]() {
                if (safeThis && safeThis->m_status != DownloadTaskStatus::Failed) {
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
    emit finished();
}

void DownloadTask::processHeadResponse()
{
    QMutexLocker locker(&m_mutex);
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
    
    if (m_headRequestTimedOut) {
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
    QPointer<DownloadTask> safeThis(this);
    QTimer::singleShot(0, this, [safeThis]() {
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
    
    // 检查请求是否已超时
    if (m_headRequestTimedOut) {
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
    emit finished();
}

void DownloadTask::createHttpWorkers()
{
    LOGD(QString("开始创建%1个HttpWorkers...").arg(m_threadCount));
    
    QMutexLocker locker(&m_mutex); // 保护m_workers
    if (m_totalSize <= 0 || m_threadCount == 1) {
        // 单线程下载
        LOGD("使用单线程下载模式");
        QString tempFilePath = m_filePath + ".part0";
        LOGD(QString("创建单线程worker，临时文件:%1").arg(tempFilePath));
        HttpWorker* worker = new HttpWorker(m_url, tempFilePath, 0, m_totalSize - 1);
        m_workers.append(worker);
        connect(worker, &HttpWorker::progress, this, &DownloadTask::onWorkerProgress);
        connect(worker, &HttpWorker::finished, this, &DownloadTask::onWorkerFinished);
        connect(worker, &HttpWorker::error, this, &DownloadTask::onWorkerError);
        LOGD("单线程worker创建完成，提交到线程池...");
        QThreadPool::globalInstance()->start(worker);
        LOGD("单线程worker已提交到线程池");
        return;
    }

    LOGD(QString("使用多线程下载模式，文件大小:%1").arg(m_totalSize));
    
    qint64 chunkSize = m_totalSize / m_threadCount;
    for (int i = 0; i < m_threadCount; ++i) {
        qint64 startPoint = i * chunkSize;
        qint64 endPoint = (i == m_threadCount - 1) ? m_totalSize - 1 : (startPoint + chunkSize - 1);
        QString tempFilePath = m_filePath + QString(".part%1").arg(i);

        LOGD(QString("创建worker%1 范围:%2-%3 临时文件:%4").arg(i).arg(startPoint).arg(endPoint).arg(tempFilePath));
        
        HttpWorker* worker = new HttpWorker(m_url, tempFilePath, startPoint, endPoint);
        m_workers.append(worker);

        connect(worker, &HttpWorker::progress, this, &DownloadTask::onWorkerProgress);
        connect(worker, &HttpWorker::finished, this, &DownloadTask::onWorkerFinished);
        connect(worker, &HttpWorker::error, this, &DownloadTask::onWorkerError);

        LOGD(QString("worker%1创建完成，提交到线程池...").arg(i));
        QThreadPool::globalInstance()->start(worker);
        LOGD(QString("worker%1已提交到线程池").arg(i));
    }
    
    LOGD("所有workers创建完成");
}

void DownloadTask::onWorkerProgress(qint64 bytes)
{
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
        LOGD("所有worker完成，开始合并文件");
        m_speedCalculationTimer.stop();
        if (mergeFiles()) {
            LOGD("文件合并成功");
            setStatus(DownloadTaskStatus::Completed);
            m_finishTime = QDateTime::currentDateTime();
            saveToHistory("Completed");
            emit finished();
            LOGD(QString("任务完成 - URL:%1").arg(m_url.toString()));
        } else {
            LOGD("文件合并失败");
            setStatus(DownloadTaskStatus::Failed);
            m_finishTime = QDateTime::currentDateTime();
            saveToHistory("Failed");
            emit error(tr("文件合并失败！"));
            emit finished();
            LOGD(QString("任务失败 - URL:%1").arg(m_url.toString()));
        }
        LOGD("开始删除临时文件");
        deleteTempFiles(); // 合并后删除临时文件
    }
}

void DownloadTask::onWorkerError(const QString& errorString)
{
    bool shouldStopWorkers = false;
    
    {
        QMutexLocker locker(&m_mutex); // 保护m_status和m_workers
        if (m_status != DownloadTaskStatus::Failed && m_status != DownloadTaskStatus::Cancelled) {
            LOGD(QString("worker出错，错误信息:%1").arg(errorString));
            setStatus(DownloadTaskStatus::Failed);
            m_speedCalculationTimer.stop();
            m_finishTime = QDateTime::currentDateTime();
            saveToHistory("Failed");
            shouldStopWorkers = true;
        }
    }
    
    if (shouldStopWorkers) {
        LOGD(QString("停止所有其他worker，总数:%1").arg(m_workers.size()));
        // 停止所有其他worker
        for (HttpWorker* worker : m_workers) {
            worker->stop();
        }
        
        // 发射信号
        emit error(tr("下载任务出错: %1").arg(errorString));
        emit finished(); // 通知管理器任务已完成（失败也是一种完成状态）
        LOGD(QString("任务错误处理完成 - URL:%1 错误:%2").arg(m_url.toString()).arg(errorString));
    }
}

void DownloadTask::onWorkerDownloadFinished()
{
    // 这个方法用于处理HttpWorker的downloadFinished信号
    // 在多线程环境下，确保对m_workers的安全访问
    QMutexLocker locker(&m_mutex); // 保护m_workers
    // 目前不需要特殊处理，因为onWorkerFinished方法已经处理了worker完成的逻辑
    // 这个信号主要用于通知事件循环退出
    LOGD("接收到worker下载完成信号");
}

void DownloadTask::onSpeedCalculationTimerTimeout()
{
    qint64 downloadedSize;
    qint64 downloadSpeed;
    
    {
        QMutexLocker locker(&m_mutex); // 保护m_downloadedSize和m_lastDownloadedSize
        m_downloadSpeed = m_downloadedSize - m_lastDownloadedSize;
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
    
    QFile finalFile(m_filePath);
    if (!prepareFinalFile(finalFile)) {
        return false;
    }

    qint64 totalBytesWritten = 0;
    qint64 totalTempFileSize = 0;
    int threadCount = getThreadCount();
    
    LOGD(QString("开始合并%1个临时文件").arg(threadCount));
    
    for (int i = 0; i < threadCount; ++i) {
        QString tempFilePath = m_filePath + QString(".part%1").arg(i);
        if (!mergeTempFile(tempFilePath, finalFile, totalBytesWritten)) {
            finalFile.close();
            return false;
        }
        totalTempFileSize += QFileInfo(tempFilePath).size();
    }
    
    finalFile.close();
    
    qint64 totalSize = getTotalSize();
    LOGD(QString("文件合并完成，总写入字节数:%1 临时文件总大小:%2 期望总大小:%3").arg(totalBytesWritten).arg(totalTempFileSize).arg(totalSize));
    
    if (!validateFinalFile(totalBytesWritten, totalSize)) {
        return false;
    }

    updateDownloadedSize(totalSize);
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
    
    for (int i = 0; i < threadCount; ++i) {
        QString tempFilePath = m_filePath + QString(".part%1").arg(i);
        bool removed = QFile::remove(tempFilePath);
        LOGD(QString("删除临时文件%1:%2 结果:%3").arg(i).arg(tempFilePath).arg(removed ? "成功" : "失败"));
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