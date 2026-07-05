#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>
#include <QByteArray>

class QLocalServer;

/**
 * @brief 单实例基础设施（QLocalServer + QLocalSocket）。
 *
 * 用途：让多个 Downloader.exe 启动共用一个进程。具体场景：
 *  - 浏览器点击 `downloader://` 协议链接 → Windows 系统调起新的 Downloader.exe 进程；
 *    第二次进程不应再开窗口，而应把 URL 转发给已经运行的 Downloader 实例（避免
 *    HttpServer 端口冲突、避免重复下载任务、避免多托盘图标）。
 *  - CLI `Downloader.exe --open <url>` 同样应转发而非重复启动。
 *
 * 设计：
 *  - 固定 serverName = "Programming666.Downloader.SingleInstance"（反向域名 + 应用标识，
 *    避免与系统其它工具冲突）。Windows 上落到命名管道 \\.\pipe\Programming666.Downloader.SingleInstance。
 *  - 第二次启动时，tryForward() 用 QLocalSocket 连这个 serverName，连得上就把
 *    payload（`downloader://<real-url>\n` 单行协议）发过去，调用方据此可 exit(0)。
 *  - 主实例启动后调用 startListening() 拿住 serverName，其它实例再尝试 tryForward 时
 *    会成功连接。
 *
 * payload 协议（v1，简单）：
 *  - 单行 ASCII，前缀 `downloader://`，紧接着真实 URL（http/https），以 `\n` 结束。
 *  - 第一版不做 HMAC 签名：在主实例上仍然校验 scheme 是 http/https，
 *    不能让本地恶意进程伪造任意路径；但这一层信任留作第二版（HMAC-nonce）。
 */
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    /// 反向域名 + 应用标识，避免与系统其它工具冲突。
    static constexpr const char* kServerName = "Programming666.Downloader.SingleInstance";

    /// payload scheme 前缀（与 ProtocolRegistrar::kScheme 同步）。用于识别 load 到的 payload。
    static constexpr const char* kPayloadPrefix = "downloader://";

    /**
     * @brief 静态工具：试图把 payload 转发给已运行的实例。
     * @param payload 单行 ASCII，建议格式：kPayloadPrefix + "<url>" + '\n'
     * @param timeoutMs connectToServer + write + waitForBytesWritten 总超时
     * @return true 表示旧实例在 timeout 内连接成功并写入成功；调用方通常据此 exit(0)。
     */
    static bool tryForward(const QByteArray& payload, int timeoutMs = 1500);

    /**
     * @brief 启动监听，绑定到 kServerName。
     * @return true 表示拿到独占（其它实例再来 tryForward 时会成功）；false 表示
     *         系统级冲突（极少 race window 出现）——调用方应当不阻塞 UI，正常继续。
     */
    bool startListening();

    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

signals:
    /**
     * @brief 主实例接收到其它进程通过 QLocalSocket 发来的 payload。
     * 槽函数负责解析（如 `kPayloadPrefix` 前缀 + 单行 URL）并入队下载任务。
     */
    void messageReceived(const QByteArray& payload);

private slots:
    void onNewConnection();

private:
    QLocalServer* m_server = nullptr;
};

#endif // SINGLEINSTANCE_H
