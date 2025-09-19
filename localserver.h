#ifndef LOCALSERVER_H
#define LOCALSERVER_H

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

/**
 * @brief LocalServer类用于创建一个本地TCP服务器，监听特定端口。
 * 主要用于接收来自浏览器插件或其他应用的下载请求。
 */
class LocalServer : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param parent 父QObject。
     */
    explicit LocalServer(QObject *parent = nullptr);
    ~LocalServer();

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
     * @param savePath 建议的保存路径（可能为空）。
     */
    void newDownloadRequest(const QString& url, const QString& savePath);

private slots:
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

private:
    QTcpServer* m_tcpServer; ///< TCP服务器实例。
};

#endif // LOCALSERVER_H
