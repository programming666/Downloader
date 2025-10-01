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
#if USE_QHTTPSERVER
    // 使用 QHttpServer 的处理函数
#else
    // 使用 QTcpServer 的处理函数
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
     * @param data HTTP请求数据
     * @param clientSocket 客户端套接字
     */
    void processHttpRequest(const QByteArray &data, QTcpSocket *clientSocket);
#endif

private:
#if USE_QHTTPSERVER
    QHttpServer* m_httpServer; ///< HTTP服务器实例。
    QList<QTcpServer*> m_tcpServers; ///< 绑定的TCP服务器列表。
#else
    QTcpServer* m_tcpServer; ///< TCP服务器实例。
#endif
};

#endif // HTTPSERVER_H