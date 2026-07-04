#ifndef HTTPSERVER_H
#define HTTPSERVER_H

#include <QObject>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

// 尝试包含QHttpServer（需要Qt 6.4+）
#ifdef QT_HTTPSERVER_LIB
#include <QHttpServer>
#define USE_QHTTPSERVER 1
#else
// 如果QHttpServer不可用，使用QTcpServer手动处理HTTP请求
#include <QTcpServer>
#include <QTcpSocket>
#define USE_QHTTPSERVER 0
#endif

/**
 * @brief HttpServer类用于创建一个本地HTTP服务器，监听特定端口。
 * 主要用于接收来自浏览器插件的下载请求。
 */
class HttpServer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param parent 父QObject。
     */
    explicit HttpServer(QObject *parent = nullptr);
    ~HttpServer();

    /**
     * @brief 启动服务器并开始监听指定端口。
     * @param port 要监听的端口号。
     * @return 如果服务器成功启动则返回true，否则返回false。
     */
    bool startServer(quint16 port);

    /**
     * @brief 停止服务器。
     */
    void stopServer();

signals:
    /**
     * @brief 当接收到新的下载请求时，发射此信号。
     * @param url 文件的URL。
     * @param savePath 建议的保存路径（可能为空，为空时使用下载器的默认路径）。
     */
    void newDownloadRequest(const QString& url, const QString& savePath);

    /**
     * @brief 当服务器启动失败时，发射此信号。
     * @param errorString 错误描述。
     */
    void error(const QString& errorString);

    /**
     * @brief 当服务器成功启动时，发射此信号。
     */
    void serverStarted();

private slots:
#if !USE_QHTTPSERVER
    /**
     * @brief 处理新的客户端连接。
     */
    void onNewConnection();

    /**
     * @brief 从客户端读取数据。
     */
    void onReadyRead();

    /**
     * @brief 处理客户端断开连接。
     */
    void onClientDisconnected();

    /**
     * @brief 解析HTTP请求并发送响应。
     * @param clientSocket 客户端套接字（buffer 由 m_buffers 取）。
     * @return true 表示本次请求已发出响应，调用方可以继续消费 pipeline 后续请求；
     *         false 表示已经安排关闭连接，调用方应停止继续读 buffer。
     */
    bool processHttpRequest(QTcpSocket *clientSocket);
#endif

private:
#if USE_QHTTPSERVER
    QHttpServer* m_httpServer;            ///< HTTP服务器实例。
    QTcpServer*  m_listenServer = nullptr; ///< 实际监听的QTcpServer，由startServer创建/释放。
#else
    QTcpServer*  m_tcpServer = nullptr;    ///< TCP服务器实例（fallback实现）。
    QHash<QTcpSocket*, QByteArray> m_buffers; ///< 每个客户端的累积缓冲区。
    QHash<QTcpSocket*, QTimer*>    m_idleTimers; ///< 每个客户端的空闲超时定时器。
#endif

    // 大小常量（fallback实现使用）
    static constexpr int MAX_BODY_SIZE     = 1 * 1024 * 1024;   ///< 请求体上限 1 MiB
    static constexpr int MAX_BUFFER_SIZE   = 2 * 1024 * 1024;   ///< 客户端累积缓冲上限 2 MiB
    static constexpr int IDLE_TIMEOUT_MS   = 30 * 1000;         ///< 慢速客户端 30s 空闲超时
    static constexpr int CLOSE_GRACE_MS    = 100;               ///< bytesWritten 之后的优雅断开延迟
    static constexpr int MAX_PENDING_CONNS = 64;                ///< Slowloris 防御
};

#endif // HTTPSERVER_H