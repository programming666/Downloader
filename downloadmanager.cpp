#include "downloadmanager.h"
#include "logger.h"
#include <QDebug>

/**
 * @brief 下载管理器构造函数
 * @param parent 父对象指针
 * 
 * 初始化下载管理器，创建线程池用于执行下载任务
 * 根据CPU核心数智能设置线程池大小，预留一个核心给UI线程
 * 确保系统响应性和下载性能的平衡
 */
DownloadManager::DownloadManager(QObject *parent)
    : QObject(parent)
{
    LOGD("开始初始化DownloadManager");
    
    m_threadPool = new QThreadPool(this);
    LOGD("QThreadPool创建完成");
    
    // 根据CPU核心数设置最大线程数，留一个核心给UI和其他任务
    int maxThreads = QThread::idealThreadCount() > 1 ? QThread::idealThreadCount() - 1 : 1;
    LOGD(QString("检测到CPU核心数:%1 设置最大线程数:%2").arg(QThread::idealThreadCount()).arg(maxThreads));
    
    m_threadPool->setMaxThreadCount(maxThreads);
    LOGD(QString("线程池最大线程数设置完成:%1").arg(m_threadPool->maxThreadCount()));
    
    LOGD("DownloadManager初始化完成");
}

DownloadManager::~DownloadManager()
{
    LOGD(QString("开始销毁DownloadManager，当前任务数:%1").arg(m_tasks.size()));
    
    // 等待所有任务完成
    LOGD("等待线程池中的任务完成...");
    m_threadPool->waitForDone();
    LOGD("线程池中的任务已全部完成");
    
    // QThreadPool的父对象是DownloadManager，会自动删除
    // m_tasks中的DownloadTask对象需要手动管理
    LOGD("开始删除所有任务对象...");
    qDeleteAll(m_tasks);
    m_tasks.clear();
    LOGD("DownloadManager销毁完成");
}

DownloadManager& DownloadManager::instance()
{
    LOGD("获取DownloadManager单例实例");
    static DownloadManager instance;
    return instance;
}

/**
 * @brief 创建新的下载任务
 * @param url 下载文件的URL
 * @param savePath 文件保存路径
 * @param threadCount 下载线程数
 * @return 创建的下载任务对象指针
 * 
 * 创建新的下载任务并添加到任务管理列表
 * 连接任务的完成和错误信号到管理器的槽函数
 * 发射taskAdded信号通知UI更新
 */
DownloadTask* DownloadManager::createTask(const QUrl& url, const QString& savePath, int threadCount)
{
    LOGD(QString("开始创建任务 - URL:%1 保存路径:%2 线程数:%3").arg(url.toString()).arg(savePath).arg(threadCount));
    
    LOGD("开始创建DownloadTask对象...");
    DownloadTask* task = new DownloadTask(url, savePath, threadCount);
    LOGD(QString("DownloadTask对象创建完成，任务指针:%1").arg(task ? "有效" : "空"));
    
    m_tasks.append(task);
    LOGD(QString("任务已添加到任务列表，总任务数:%1").arg(m_tasks.size()));
    
    LOGD("开始连接任务信号...");
    connect(task, &DownloadTask::finished, this, &DownloadManager::onTaskFinished);
    connect(task, &DownloadTask::error, this, &DownloadManager::onTaskError);
    LOGD("任务信号连接完成");
    
    LOGD("准备发射taskAdded信号...");
    emit taskAdded(task);
    LOGD("taskAdded信号已发射");
    
    LOGD("任务创建完成，返回任务指针");
    
    return task;
}

void DownloadManager::startTask(DownloadTask* task)
{
    LOGD(QString("开始启动任务，任务指针:%1").arg(task ? "有效" : "空"));
    
    if (task) {
        LOGD(QString("任务有效，文件名:%1 URL:%2").arg(task->fileName()).arg(task->url()));
        LOGD("调用task->start()...");
        task->start();
        LOGD("task->start()调用完成");
    } else {
        LOGD("任务指针为空，无法启动");
    }
    
    LOGD("startTask方法执行完成");
}

void DownloadManager::pauseTask(DownloadTask* task)
{
    LOGD(QString("暂停任务，任务指针:%1").arg(task ? "有效" : "空"));
    if (task) {
        LOGD(QString("调用task->pause()，文件名:%1").arg(task->fileName()));
        task->pause();
        LOGD("task->pause()调用完成");
    }
}

void DownloadManager::resumeTask(DownloadTask* task)
{
    LOGD(QString("恢复任务，任务指针:%1").arg(task ? "有效" : "空"));
    if (task) {
        LOGD(QString("调用task->resume()，文件名:%1").arg(task->fileName()));
        task->resume();
        LOGD("task->resume()调用完成");
    }
}

void DownloadManager::cancelTask(DownloadTask* task, bool deleteFile)
{
    LOGD(QString("取消任务，任务指针:%1 删除文件:%2").arg(task ? "有效" : "空").arg(deleteFile));
    if (task) {
        LOGD(QString("调用task->cancel()，文件名:%1").arg(task->fileName()));
        task->cancel(deleteFile);
        // cancel()会触发finished()信号，在onTaskFinished()中处理后续
        LOGD("task->cancel()调用完成");
    }
}

QThreadPool* DownloadManager::threadPool() const
{
    LOGD("返回线程池指针");
    return m_threadPool;
}

void DownloadManager::onTaskFinished()
{
    LOGD("接收到任务完成信号");
    
    DownloadTask* task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        LOGD(QString("任务有效，文件名:%1 状态:%2").arg(task->fileName()).arg(static_cast<int>(task->status())));
        
        LOGD("发射taskFinished信号...");
        emit taskFinished(task);
        LOGD("taskFinished信号已发射");
        
        LOGD("从任务列表中移除任务...");
        m_tasks.removeOne(task);
        LOGD(QString("任务已从列表中移除，剩余任务数:%1").arg(m_tasks.size()));
        
        LOGD("标记任务为延迟删除...");
        task->deleteLater(); // 任务完成后安全删除
        LOGD("任务已标记为延迟删除");
    } else {
        LOGD("sender不是有效的DownloadTask对象");
    }
    
    LOGD("onTaskFinished处理完成");
}

void DownloadManager::onTaskError(const QString& errorString)
{
    LOGD(QString("接收到任务错误信号，错误信息:%1").arg(errorString));
    
    DownloadTask* task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        LOGD(QString("任务有效，文件名:%1").arg(task->fileName()));
        
        LOGD("发射taskError信号...");
        emit taskError(task, errorString);
        LOGD("taskError信号已发射");
        
        // 错误发生后，任务也算"完成"，从列表中移除
        LOGD("从任务列表中移除错误任务...");
        m_tasks.removeOne(task);
        LOGD(QString("错误任务已从列表中移除，剩余任务数:%1").arg(m_tasks.size()));
        
        LOGD("标记错误任务为延迟删除...");
        task->deleteLater();
        LOGD("错误任务已标记为延迟删除");
    } else {
        LOGD("sender不是有效的DownloadTask对象");
    }
    
    LOGD("onTaskError处理完成");
}