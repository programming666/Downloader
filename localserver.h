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
 *
 * @attention 协议说明：本类使用 **裸 JSON 协议**（客户端直接发送 JSON 对象，
 * 服务端回复一行 JSON），与 HttpServer 使用的 HTTP/JSON 协议不同。
 * 当前 main.cpp 仅实例化 HttpServer，本类处于备用状态，保留以备未来切换。
 * 如果启用本类，请确保客户端按如下格式通信：
 *   请求（任意时刻 TCP 上发送一个 JSON 对象）：
 *     {"url": "https://...", "savePath": "/optional/path"}
 *   响应（服务端写一行 JSON + 换行）：
 *     {"status":"success","message":"..."}
 *
 * @warning 安全约束：LocalServer 在 v0.1 之前接受任意 TCP/JSON 输入而
 * 不做认证或 URL/文件名校验，曾暴露 "任何人发请求即可触发任意下载" 的
 * CSRF/SSRF 漏洞。当前实现已**引入**与 HttpServer 对齐的校验：
 *   - URL 必须走 isValidDownloadUrl（拒绝 loopback/private/rDNS 隧道）
 *   - filename 必须走 isSafeFileName（拒绝 Windows 保留名/控制字符/盘符）
 *   - HTTP 缺失时使用一个进程级 bearer token（与 HttpServer 同源）
 *   - Body 上限 1 MiB，超额直接拒绝并断开
 *   - JSON 字段必须为字符串；额外字段忽略但拒绝非字符串值
 * 仅当启用本类（即 main.cpp 实例化 LocalServer）才走这些校验；保留类
 * 是为了未来按需切换实现，**默认请使用 HttpServer**。
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
