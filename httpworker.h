#ifndef HTTPWORKER_H
#define HTTPWORKER_H

#include <QObject>
#include <QRunnable>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QFile>
#include <QUrl>
#include <QDebug>
#include <QEventLoop>
#include <atomic>

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
     * @brief 读取本 worker 累计已接收字节数（原子读，跨线程安全）。
     * 用于 DownloadTask 的 200ms 定时器在主线程汇总所有 worker 的进度，
     * HEAD 没拿到 Content-Length 时用此值估算"已下载多少"。
     */
    qint64 bytesReceivedAtomic() const { return m_bytesReceived.load(std::memory_order_acquire); }

    /**
     * @brief 重置HttpWorker状态，允许重新启动下载（用于断点续传）
     */
    void reset();

    /**
     * @brief QRunnable的运行方法，在线程池中执行
     */
    void run() override;

    /**
     * @brief 停止下载。
     */
    void stop();

    /**
     * @brief 异步停止下载。
     */
    void stopAsync();

    /**
     * @brief 退出 worker 线程内的 QEventLoop。线程安全：
     *  - 在 worker 线程里直接 quit；
     *  - 从其他线程调用时用 invokeMethod 切到 worker 线程执行。
     */
    void quitLoop();

    /**
     * @brief 更新 worker 内部 QNAM 的代理设置。
     *
     * DownloadTask::applyProxy() 在主线程调用本函数。QNAM 由 worker 在主线程
     * 通过 QTimer::singleShot(0, qApp, ...) 创建（见 HttpWorker::startDownload），
     * 因此 setProxy 从主线程直接同步调用是安全的——Qt 的 QNetworkAccessManager
     * 内部对此做了线程安全处理，对正在进行的请求不会打断，新请求会带上新代理。
     *
     * @param proxy 新的代理设置（type 字段需已正确设置）。
     */
    void setProxy(const QNetworkProxy& proxy);

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
    std::atomic<qint64> m_bytesReceived;     ///< 本会话已接收的字节数（原子类型，跨线程安全，支持>2GB文件）。
    qint64 m_resumeOffset{0};       ///< 从磁盘续传时检测到的已有字节数（仅一次设置，避免 m_bytesReceived 语义混淆）。

    QNetworkAccessManager* m_netManager; ///< 网络访问管理器。
    QNetworkReply* m_reply;         ///< 网络应答。
    QFile* m_file;                  ///< 临时文件。

    bool m_isStopped;               ///< 标记是否已停止。
    int m_retryCount;               ///< 当前重试次数（实例成员，避免跨worker共享）。
    bool m_alreadyFinished;         ///< 标记finished/error是否已发射，避免重复发射。
    qint64 m_lastLoggedBytes{0};    ///< 上次记录日志时的字节数（实例成员，避免跨worker共享）。

    /// run() 内部事件循环：让 QNetworkReply 的 readyRead/finished 等信号在 worker
    /// 线程的事件循环里被消化。run() 入口 moveToThread 后，this->thread() == worker
    /// 线程，QNetworkAccessManager 投递的信号会进入这个 loop。
    QEventLoop* m_loop = nullptr;
};

#endif // HTTPWORKER_H
