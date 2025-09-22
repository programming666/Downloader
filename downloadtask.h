#ifndef DOWNLOADTASK_H
#define DOWNLOADTASK_H

#include <QObject>
#include <QUrl>
#include <QList>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFileInfo>
#include <QDateTime>
#include <QTimer>
#include <QMutex>
#include <QEventLoop>
#include "httpworker.h"
//#include "historymanager.h" // 包含历史管理器头文件

/**
 * @brief DownloadTaskStatus枚举定义了下载任务的各种状态。
 */
enum class DownloadTaskStatus {
    Pending,    ///< 等待中
    Downloading,///< 正在下载
    Paused,     ///< 已暂停
    Cancelled,  ///< 已取消
    Completed,  ///< 已完成
    Failed      ///< 失败
};

/**
 * @brief DownloadTask类代表一个独立的下载任务。
 * 它负责获取文件信息、分块、调度HttpWorker、合并文件以及管理任务状态。
 */
class DownloadTask : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param url 文件的URL。
     * @param savePath 文件保存的本地路径。
     * @param threadCount 下载使用的线程数。
     * @param parent 父QObject。
     */
    explicit DownloadTask(const QUrl& url, const QString& savePath, int threadCount, QObject *parent = nullptr);
    ~DownloadTask();

    /**
     * @brief 启动下载任务。
     */
    void start();

    /**
     * @brief 暂停下载任务。
     */
    void pause();

    /**
     * @brief 恢复下载任务。
     */
    void resume();

    /**
     * @brief 取消下载任务。
     * @param deleteTempFiles 是否删除所有临时文件。
     */
    void cancel(bool deleteTempFiles = true);

    /**
     * @brief 获取当前任务的状态。
     * @return DownloadTaskStatus枚举值。
     */
    DownloadTaskStatus status() const { return m_status; }

    /**
     * @brief 获取任务的URL。
     * @return URL字符串。
     */
    QString url() const { return m_url.toString(); }

    /**
     * @brief 获取文件保存路径。
     * @return 文件路径字符串。
     */
    QString filePath() const { return m_filePath; }

    /**
     * @brief 获取文件名。
     * @return 文件名字符串。
     */
    QString fileName() const { return m_fileName; }

    /**
     * @brief 获取文件总大小。
     * @return 文件总大小（字节）。
     */
    qint64 totalSize() const { return m_totalSize; }

    /**
     * @brief 获取已下载大小。
     * @return 已下载大小（字节）。
     */
    qint64 downloadedSize() const { return m_downloadedSize; }

    /**
     * @brief 获取下载进度百分比。
     * @return 进度百分比（0-100）。
     */
    int progressPercentage() const;

    /**
     * @brief 获取当前下载速度。
     * @return 下载速度（字节/秒）。
     */
    qint64 downloadSpeed() const { return m_downloadSpeed; }

signals:
    /**
     * @brief 当任务状态改变时发射此信号。
     * @param status 新的任务状态。
     */
    void statusChanged(DownloadTaskStatus status);

    /**
     * @brief 当下载进度更新时发射此信号。
     * @param bytesReceived 已下载的字节数。
     * @param totalBytes 文件总字节数。
     * @param speed 当前下载速度。
     */
    void progressUpdated(qint64 bytesReceived, qint64 totalBytes, qint64 speed);

    /**
     * @brief 当任务完成时发射此信号。
     */
    void finished();

    /**
     * @brief 当任务出现错误时发射此信号。
     * @param errorString 错误信息。
     */
    void error(const QString& errorString);

private slots:
    /**
     * @brief 处理HEAD请求完成的槽函数，用于获取文件信息。
     */
    void onHeadRequestFinished();

    /**
     * @brief 处理HEAD请求出错的槽函数。
     * @param code 错误码。
     */
    void onHeadRequestError(QNetworkReply::NetworkError code);

    /**
     * @brief 处理HttpWorker的进度更新信号。
     * @param bytes 本次HttpWorker下载的字节数。
     */
    void onWorkerProgress(qint64 bytes);

    /**
     * @brief 处理HttpWorker完成的信号。
     */
    void onWorkerFinished();

    /**
     * @brief 处理HttpWorker出错的信号。
     * @param errorString 错误信息。
     */
    void onWorkerError(const QString& errorString);

    /**
     * @brief 处理HttpWorker下载完成的信号。
     */
    void onWorkerDownloadFinished();

    /**
     * @brief 定时器槽函数，用于计算下载速度和更新UI。
     */
    void onSpeedCalculationTimerTimeout();

private:
    /**
     * @brief 设置任务状态并发射statusChanged信号。
     * @param newStatus 新的任务状态。
     */
    void setStatus(DownloadTaskStatus newStatus);

    /**
     * @brief 初始化下载任务，包括获取文件信息和创建HttpWorker。
     */
    void initializeDownload();

    /**
     * @brief 分割文件并创建HttpWorker。
     */
    void createHttpWorkers();

    /**
     * @brief 检查所有HttpWorker是否都已完成。
     * @return 如果所有worker都完成则返回true，否则返回false。
     */
    bool allWorkersFinished() const;

    /**
     * @brief 合并所有临时文件到最终文件。
     * @return 如果合并成功则返回true，否则返回false。
     */
    bool mergeFiles();

    /**
     * @brief 删除所有临时文件。
     */
    void deleteTempFiles();

    /**
     * @brief 处理HEAD请求错误。
     * @param errorString 错误信息。
     */
    void handleHeadRequestError(const QString& errorString);

    /**
     * @brief 处理HEAD响应。
     */
    void processHeadResponse();

    /**
     * @brief 准备最终文件。
     * @param finalFile 最终文件对象。
     * @return 准备成功返回true，否则返回false。
     */
    bool prepareFinalFile(QFile& finalFile);

    /**
     * @brief 合并单个临时文件。
     * @param tempFilePath 临时文件路径。
     * @param finalFile 最终文件对象。
     * @param totalBytesWritten 累计写入字节数。
     * @return 合并成功返回true，否则返回false。
     */
    bool mergeTempFile(const QString& tempFilePath, QFile& finalFile, qint64& totalBytesWritten);

    /**
     * @brief 验证最终文件。
     * @param totalBytesWritten 总写入字节数。
     * @param expectedSize 期望文件大小。
     * @return 验证成功返回true，否则返回false。
     */
    bool validateFinalFile(qint64 totalBytesWritten, qint64 expectedSize);

    /**
     * @brief 获取线程数。
     * @return 线程数。
     */
    int getThreadCount() const;

    /**
     * @brief 获取文件总大小。
     * @return 文件总大小。
     */
    qint64 getTotalSize() const;

    /**
     * @brief 更新已下载大小。
     * @param size 新的已下载大小。
     */
    void updateDownloadedSize(qint64 size);

    /**
     * @brief 将任务记录保存到历史管理器。
     * @param status 任务的最终状态。
     */
    void saveToHistory(const QString& status);

    QUrl m_url;                         ///< 下载文件的URL。
    QString m_filePath;                 ///< 文件保存的本地路径。
    QString m_fileName;                 ///< 文件名。
    int m_threadCount;                  ///< 下载线程数。
    DownloadTaskStatus m_status;        ///< 当前任务状态。

    qint64 m_totalSize;                 ///< 文件总大小。
    qint64 m_downloadedSize;            ///< 已下载大小。
    qint64 m_lastDownloadedSize;        ///< 上次计算速度时的已下载大小。
    qint64 m_downloadSpeed;             ///< 当前下载速度。
    QDateTime m_startTime;              ///< 任务开始时间。
    QDateTime m_finishTime;             ///< 任务完成时间。

    QNetworkAccessManager* m_headManager; ///< 用于发送HEAD请求获取文件信息的网络管理器。
    QNetworkReply* m_headReply;         ///< HEAD请求的应答。

    QList<HttpWorker*> m_workers;       ///< HttpWorker列表。
    int m_finishedWorkers;              ///< 已完成的HttpWorker数量。
    QTimer m_speedCalculationTimer;     ///< 用于计算下载速度的定时器。
    mutable QMutex m_mutex;                     ///< 用于保护worker列表等数据的互斥锁
    mutable QMutex m_statusMutex;               ///< 专门保护状态变量的互斥锁
    mutable QMutex m_historyMutex;              ///< 专门保护历史记录操作的互斥锁
    bool m_headRequestTimedOut{false};  ///< 标记HEAD请求是否已超时。
};

#endif // DOWNLOADTASK_H
