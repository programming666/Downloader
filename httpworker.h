#ifndef HTTPWORKER_H
#define HTTPWORKER_H

#include <QObject>
#include <QRunnable>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QUrl>
#include <QDebug>
#include <QEventLoop>

/**
 * @brief HttpWorker类是执行文件分块下载的实际工作单元。
 * 它是纯QObject，在主线程中异步执行网络请求。
 * 每个HttpWorker负责下载文件的一个特定字节范围。
 */
class HttpWorker : public QObject, public QRunnable
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param url 文件的URL。
     * @param filePath 保存下载数据的临时文件路径。
     * @param startPoint 下载范围的起始字节。
     * @param endPoint 下载范围的结束字节。
     * @param parent 父QObject。
     */
    explicit HttpWorker(const QUrl& url, const QString& filePath, qint64 startPoint, qint64 endPoint);
    ~HttpWorker();

    /**
     * @brief QRunnable的运行方法，在线程池中执行
     */
    void run() override;

    /**
     * @brief 停止下载。
     */
    void stop();

signals:
    /**
     * @brief 当下载完成时发射此信号。
     */
    void finished();

    /**
     * @brief 当下载进度更新时发射此信号。
     * @param bytesReceived 本次接收到的字节数。
     */
    void progress(qint64 bytesReceived);

    /**
     * @brief 当发生错误时发射此信号。
     * @param errorString 错误信息。
     */
    void error(const QString& errorString);
    

private slots:
    /**
     * @brief 处理网络应答的readyRead信号，将数据写入文件。
     */
    void onReadyRead();

    /**
     * @brief 处理网络应答的finished信号。
     */
    void onFinished();

    /**
     * @brief 处理网络应答的errorOccurred信号。
     * @param code 错误码。
     */
    void onErrorOccurred(QNetworkReply::NetworkError code);

private:
    /**
     * @brief 在主线程中开始下载。
     */
    void startDownload();
    
    /**
     * @brief 继续执行下载逻辑。
     */
    void continueDownload();
    
    /**
     * @brief 清理资源。
     */
    void cleanup();

private:
    QUrl m_url;                     ///< 文件的URL。
    QString m_filePath;             ///< 临时文件的路径。
    qint64 m_startPoint;            ///< 下载范围的起始点。
    qint64 m_endPoint;              ///< 下载范围的结束点。
    qint64 m_bytesReceived;         ///< 已接收的字节数。

    QNetworkAccessManager* m_netManager; ///< 网络访问管理器。
    QNetworkReply* m_reply;         ///< 网络应答。
    QFile* m_file;                  ///< 临时文件。

    bool m_isStopped;               ///< 标记是否已停止。
};

#endif // HTTPWORKER_H
