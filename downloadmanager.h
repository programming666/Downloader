#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QObject>
#include <QThreadPool>
#include <QList>
#include "downloadtask.h"

/**
 * @brief DownloadManager类是下载任务的核心调度中心。
 * 这是一个单例类，负责创建、管理和调度所有的DownloadTask。
 * 它维护一个全局线程池以高效地执行下载工作单元。
 */
class DownloadManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取DownloadManager的单例实例。
     * @return DownloadManager的唯一实例。
     */
    static DownloadManager& instance();

    // 禁用拷贝构造函数和赋值运算符
    DownloadManager(const DownloadManager&) = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    /**
     * @brief 创建一个新的下载任务。
     * @param url 文件的URL。
     * @param savePath 文件的保存路径。
     * @param threadCount 使用的线程数。
     * @return 返回创建的DownloadTask指针。任务创建后需要手动调用startTask来启动。
     */
    DownloadTask* createTask(const QUrl& url, const QString& savePath, int threadCount);

    /**
     * @brief 启动一个下载任务。
     * @param task 要启动的DownloadTask指针。
     */
    void startTask(DownloadTask* task);

    /**
     * @brief 暂停一个下载任务。
     * @param task 要暂停的DownloadTask指针。
     */
    void pauseTask(DownloadTask* task);

    /**
     * @brief 恢复一个下载任务。
     * @param task 要恢复的DownloadTask指针。
     */
    void resumeTask(DownloadTask* task);

    /**
     * @brief 取消一个下载任务。
     * @param task 要取消的DownloadTask指针。
     * @param deleteFile 是否删除已下载的临时文件。
     */
    void cancelTask(DownloadTask* task, bool deleteFile = true);

    /**
     * @brief 获取全局的线程池实例。
     * @return QThreadPool的指针。
     */
    QThreadPool* threadPool() const;

signals:
    /**
     * @brief 当一个任务被添加到管理器时发射此信号。
     * @param task 被添加的DownloadTask指针。
     */
    void taskAdded(DownloadTask* task);

    /**
     * @brief 当一个任务完成时发射此信号。
     * @param task 已完成的DownloadTask指针。
     */
    void taskFinished(DownloadTask* task);

    /**
     * @brief 当一个任务出现错误时发射此信号。
     * @param task 出现错误的DownloadTask指针。
     * @param errorString 错误信息。
     */
    void taskError(DownloadTask* task, const QString& errorString);

private slots:
    /**
     * @brief 处理任务完成的槽函数。
     */
    void onTaskFinished();

    /**
     * @brief 处理任务出错的槽函数。
     * @param errorString 错误信息。
     */
    void onTaskError(const QString& errorString);

private:
    /**
     * @brief 私有构造函数，确保单例模式。
     * @param parent 父QObject。
     */
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager();

    QThreadPool* m_threadPool;          ///< 全局线程池。
    QList<DownloadTask*> m_tasks;       ///< 当前活动的下载任务列表。
};

#endif // DOWNLOADMANAGER_H
