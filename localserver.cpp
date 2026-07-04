// SECURITY WARNING:
//   This class had no URL/filename validation and no auth, which combined
//   with its bare TCP/JSON framing meant anyone able to reach the local
//   port could trigger arbitrary downloads (CSRF/SSRF). The implementations
//   below now mirror the validation patterns used by HttpServer (origin
//   guard via bearer token, URL/SSRF check, filename-safety check, body
//   size cap). main.cpp does NOT instantiate LocalServer; HttpServer is the
//   active code path. Keep LocalServer only as a backup or for custom
//   clients that speak the documented JSON-over-TCP protocol.
// =========================================================================

#include "localserver.h"
#include "logger.h"
#include "settingsmanager.h"
#include <QDebug>
#include <QHostAddress>
#include <QHostInfo>
#include <QUrl>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QPointer>

namespace {

// 与 HttpServer 对齐：导入 SettingsManager 暴露的 bearerToken()。
// 若首次启动还没有持久化 token，bearerToken() 会生成并保存一个 UUID。
static QByteArray currentBearerToken()
{
    return SettingsManager::instance().bearerToken().toUtf8();
}

// 与 HttpServer 一致的私有 / 回环 / 多播 / 测试网段拒绝。
static bool isPrivateOrLoopbackIp(const QHostAddress &addr)
{
    if (addr.isLoopback())  return true;
    if (addr.isLinkLocal()) return true;
    if (addr.isMulticast()) return true;

    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = addr.toIPv4Address();
        const quint32 b1 = (ip >> 24) & 0xFFu;
        const quint32 b2 = (ip >> 16) & 0xFFu;
        if (b1 == 0u) return true;
        if (b1 == 10u) return true;
        if (b1 == 100u && b2 >= 64u && b2 <= 127u) return true;
        if (b1 == 127u) return true;
        if (b1 == 172u && b2 >= 16u && b2 <= 31u) return true;
        if (b1 == 192u && b2 == 168u) return true;
        if ((b1 == 192u && b2 == 0u  && ((ip >> 8) & 0xFFu) == 2u)  ||
            (b1 == 198u && b2 == 51u && ((ip >> 8) & 0xFFu) == 100u) ||
            (b1 == 203u && b2 == 0u  && ((ip >> 8) & 0xFFu) == 113u))
            return true;
        if (b1 == 198u && (b2 == 18u || b2 == 19u)) return true;
        if (b1 >= 240u) return true;
        return false;
    }

    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR ip6 = addr.toIPv6Address();
        if ((ip6[0] & 0xFEu) == 0xFCu) return true;
        // IPv4-mapped IPv6: bytes 0-9 == 0, bytes 10-11 == 0xff (RFC 4291 §2.5.5.2).
        // Qt 6.11 removed QHostAddress::isIPv4Mapped(); check the bytes directly.
        const bool isV4Mapped = (ip6[0]  == 0 && ip6[1]  == 0 && ip6[2]  == 0 &&
                                 ip6[3]  == 0 && ip6[4]  == 0 && ip6[5]  == 0 &&
                                 ip6[6]  == 0 && ip6[7]  == 0 && ip6[8]  == 0 &&
                                 ip6[9]  == 0 && ip6[10] == 0xff && ip6[11] == 0xff);
        if (isV4Mapped) {
            const quint32 mapped = (quint32(ip6[12]) << 24) | (quint32(ip6[13]) << 16)
                                 | (quint32(ip6[14]) << 8)  |  quint32(ip6[15]);
            return isPrivateOrLoopbackIp(QHostAddress(mapped));
        }
        return false;
    }
    return true;
}

static bool hostResolvesToPublicAddress(const QString &host, QString *errorOut)
{
    if (host.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty host");
        return false;
    }
    // 旁路：环境变量启用本地回环放行（与 httpserver.cpp 同步）。公网生产
    // 部署不设置该环境变量，SSRF 拦截仍然生效。
    static const bool allowLoopback = []() {
        const QByteArray v = qgetenv("DOWNLOADER_ALLOW_LOOPBACK");
        if (v.isEmpty()) return false;
        const QString s = QString::fromLocal8Bit(v).trimmed().toLower();
        return s == "1" || s == "true" || s == "yes";
    }();
    if (allowLoopback) {
        return true;
    }
    QHostAddress literal;
    if (literal.setAddress(host)) {
        if (isPrivateOrLoopbackIp(literal)) {
            if (errorOut) *errorOut = QStringLiteral("host resolves to private/loopback address");
            return false;
        }
        return true;
    }
    QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError) {
        if (errorOut) *errorOut = QStringLiteral("DNS lookup failed: %1").arg(info.errorString());
        return false;
    }
    const QList<QHostAddress> addrs = info.addresses();
    if (addrs.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("DNS lookup returned no addresses");
        return false;
    }
    for (const QHostAddress &a : addrs) {
        if (isPrivateOrLoopbackIp(a)) {
            if (errorOut) *errorOut = QStringLiteral("host resolves to private/loopback address");
            return false;
        }
    }
    return true;
}

static bool isValidDownloadUrl(const QString &rawUrl, QString *errorOut)
{
    if (rawUrl.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty url");
        return false;
    }
    const QUrl url(rawUrl);
    if (!url.isValid()) {
        if (errorOut) *errorOut = QStringLiteral("invalid url");
        return false;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        if (errorOut) *errorOut = QStringLiteral("only http/https schemes are allowed");
        return false;
    }
    const QString host = url.host();
    if (host.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("missing host");
        return false;
    }
    return hostResolvesToPublicAddress(host, errorOut);
}

static bool isReservedWindowsName(const QString &base)
{
    static const QSet<QString> kReserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),
    };
    const QString up = base.toUpper();
    if (kReserved.contains(up)) return true;
    static const QRegularExpression kComLpt(QStringLiteral("^(COM|LPT)[1-9]$"));
    return kComLpt.match(up).hasMatch();
}

static bool isSafeFileName(const QString &raw, QString *errorOut)
{
    QString name = raw.trimmed();
    if (name.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty filename");
        return false;
    }
    if (name.size() > 200) {
        if (errorOut) *errorOut = QStringLiteral("filename too long (max 200)");
        return false;
    }
    static const QSet<QChar> kForbiddenChars = {
        '<', '>', ':', '"', '/', '\\', '|', '?', '*'
    };
    for (QChar c : name) {
        if (c.isNull() || c.unicode() < 0x20) {
            if (errorOut) *errorOut = QStringLiteral("control/NUL characters not allowed");
            return false;
        }
        if (kForbiddenChars.contains(c)) {
            if (errorOut) *errorOut = QStringLiteral("forbidden character in filename");
            return false;
        }
    }
    QString basename = name;
    const int lastSlash = qMax(basename.lastIndexOf('/'), basename.lastIndexOf('\\'));
    if (lastSlash >= 0) basename = basename.mid(lastSlash + 1);
    if (basename.size() >= 2 && basename[1] == QLatin1Char(':') &&
        ((basename[0] >= QLatin1Char('A') && basename[0] <= QLatin1Char('Z')) ||
         (basename[0] >= QLatin1Char('a') && basename[0] <= QLatin1Char('z')))) {
        if (errorOut) *errorOut = QStringLiteral("drive letter not allowed");
        return false;
    }
    if (basename.endsWith(QLatin1Char('.')) || basename.endsWith(QLatin1Char(' '))) {
        if (errorOut) *errorOut = QStringLiteral("filename must not end with '.' or space");
        return false;
    }
    const QString stem = basename.section(QLatin1Char('.'), 0, 0);
    if (isReservedWindowsName(stem)) {
        if (errorOut) *errorOut = QStringLiteral("reserved Windows device name");
        return false;
    }
    if (basename == QStringLiteral(".") || basename == QStringLiteral("..")) {
        if (errorOut) *errorOut = QStringLiteral("invalid path segment");
        return false;
    }
    return true;
}

// 与 HttpServer 一致：严格提取字符串字段；非字符串/缺失必填字段都拒绝。
static bool extractStringField(const QJsonObject &obj, const QString &key,
                               QString *out, QString *errorOut, bool required)
{
    if (!obj.contains(key)) {
        if (required) {
            if (errorOut) *errorOut = QStringLiteral("missing required field '%1'").arg(key);
            return false;
        }
        out->clear();
        return true;
    }
    const QJsonValue v = obj.value(key);
    if (!v.isString()) {
        if (errorOut) *errorOut = QStringLiteral("field '%1' must be a string").arg(key);
        return false;
    }
    *out = v.toString();
    return true;
}

// 自定义的 JSON 一行响应。区别于 HTTP：仅一行 JSON + \n；
// 这也是 LocalServer 与 HttpServer 的协议差异点。
static QByteArray jsonLine(const char *status, const QString &message)
{
    QJsonObject o;
    o.insert(QStringLiteral("status"),  QString::fromUtf8(status));
    o.insert(QStringLiteral("message"), message);
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

// 单连接 body 上限 1 MiB：与 HttpServer 的 MAX_BODY_SIZE 一致。
constexpr qint64 MAX_BODY_SIZE = 1 * 1024 * 1024;

} // namespace

/**
 * @brief 本地服务器构造函数
 * @param parent 父对象指针
 *
 * 创建TCP服务器实例，用于接收浏览器插件的下载请求
 * 服务器监听本地端口，接收JSON格式的下载任务请求
 */
LocalServer::LocalServer(QObject *parent)
    : QObject(parent)
    , m_tcpServer(nullptr)
{
    LOGD("开始初始化LocalServer");

    LOGD("开始创建QTcpServer...");
    m_tcpServer = new QTcpServer(this);
    LOGD("QTcpServer创建完成");

    LOGD("开始连接信号...");
    connect(m_tcpServer, &QTcpServer::newConnection, this, &LocalServer::onNewConnection);
    LOGD("信号连接完成");

    LOGD("LocalServer初始化完成");
}

/**
 * @brief 本地服务器析构函数
 *
 * 确保在对象销毁时停止服务器，释放资源
 */
LocalServer::~LocalServer()
{
    stopServer();
}

/**
 * @brief 启动本地服务器
 * @param port 监听端口号
 * @return true 启动成功，false 启动失败
 *
 * 在指定端口启动TCP服务器监听，只接受本地连接
 * 如果服务器已经在运行，则先关闭再重启（端口重用）
 */
bool LocalServer::startServer(quint16 port)
{
    LOGD(QString("开始启动本地服务器 - 端口:%1").arg(port));

    // 端口绑定竞态：startServer 前先确保 close，避免前一会话残留。
    if (m_tcpServer->isListening()) {
        LOGD("服务器已在监听状态，停止当前监听");
        m_tcpServer->close();
    }

    LOGD("开始监听...");
    bool result = m_tcpServer->listen(QHostAddress::LocalHost, port);
    LOGD(QString("监听结果:%1").arg(result ? "成功" : "失败"));

    if (!result) {
        LOGD(QString("监听失败 - 错误:%1").arg(m_tcpServer->errorString()));
        emit error(QString("无法启动服务器: %1").arg(m_tcpServer->errorString()));
        return false;
    }

    LOGD(QString("本地服务器启动成功 - 监听端口:%1").arg(port));
    emit serverStarted();
    return true;
}

void LocalServer::stopServer()
{
    if (m_tcpServer->isListening()) {
        m_tcpServer->close();
        qDebug() << "Local server stopped.";
    }
}

/**
 * @brief 处理新的客户端连接
 *
 * 当有新客户端连接到服务器时触发
 * 限制 pending 连接数，与 HttpServer 的 Slowloris 防御一致
 */
void LocalServer::onNewConnection()
{
    // Slowloris 防御：限制挂起连接上限。
    // HttpServer 中也是 32；这里复用同样量级。
    constexpr int MAX_PENDING_CONNS = 32;
    int accepted = 0;
    while (QTcpSocket *clientSocket = m_tcpServer->nextPendingConnection()) {
        if (accepted >= MAX_PENDING_CONNS) {
            clientSocket->abort();
            clientSocket->deleteLater();
            continue;
        }
        ++accepted;

        // 让 socket 跟随本对象（避免 dangling key）。
        clientSocket->setParent(this);

        connect(clientSocket, &QTcpSocket::readyRead, this, &LocalServer::onReadyRead);
        connect(clientSocket, &QTcpSocket::disconnected, this, &LocalServer::onClientDisconnected);

        // 30s 空闲超时：防止 Slowloris 攻击挂着不发数据。
        QTimer *idleTimer = new QTimer(clientSocket);
        idleTimer->setSingleShot(true);
        idleTimer->setInterval(30 * 1000);
        connect(idleTimer, &QTimer::timeout, clientSocket, [clientSocket]() {
            qWarning() << "[LocalServer] idle timeout, dropping peer";
            clientSocket->abort();
            clientSocket->deleteLater();
        });
        idleTimer->start();

        qDebug() << "New client connected:" << clientSocket->peerAddress().toString();
    }
}

/**
 * @brief 处理客户端发送的数据
 *
 * 当客户端有数据可读时触发，读取并解析JSON格式的下载请求
 *
 * 安全校验（与 HttpServer 对齐）：
 *   1) Body 大小 <= 1 MiB
 *   2) JSON 必须可解析且为 object
 *   3) "url" 必填且必须通过 isValidDownloadUrl（拒绝 loopback/private/rDNS）
 *   4) "filename" 可选但若有必须通过 isSafeFileName
 *   5) "savePath" 可选但必须为字符串（防 obj.value 默认 toString）
 *   6) "token" 必填，且必须等于 SettingsManager::bearerToken() 输出
 *      （等价于 HttpServer 的 Authorization: Bearer <token>）
 */
void LocalServer::onReadyRead()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) return;

    // 读光缓冲区但限制单次请求体积，防止 OOM。
    QByteArray data = clientSocket->readAll();
    if (data.size() > MAX_BODY_SIZE) {
        qWarning() << "[LocalServer] body too large:" << data.size();
        QPointer<QTcpSocket> safe(clientSocket);
        clientSocket->write(jsonLine("error", QStringLiteral("request body too large")));
        QTimer::singleShot(50, clientSocket, [safe]() {
            if (safe) safe->disconnectFromHost();
        });
        return;
    }

    // 解析JSON数据
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
        clientSocket->write(jsonLine("error", QStringLiteral("Invalid JSON")));
        clientSocket->disconnectFromHost();
        return;
    }
    if (!doc.isObject()) {
        clientSocket->write(jsonLine("error", QStringLiteral("JSON is not an object")));
        clientSocket->disconnectFromHost();
        return;
    }

    const QJsonObject obj = doc.object();

    // 鉴权：bearer token。HttpServer 是放在 Authorization 头里，
    // 这里因为协议只有 body，所以放在 JSON 字段 "token" 中。
    QString presentedToken;
    QString err;
    if (!extractStringField(obj, QStringLiteral("token"), &presentedToken, &err, /*required=*/true)) {
        clientSocket->write(jsonLine("error", err));
        clientSocket->disconnectFromHost();
        return;
    }
    if (presentedToken.isEmpty() || presentedToken.toUtf8() != currentBearerToken()) {
        qWarning() << "[LocalServer] rejected: missing/invalid bearer token";
        clientSocket->write(jsonLine("error", QStringLiteral("missing or invalid bearer token")));
        clientSocket->disconnectFromHost();
        return;
    }

    // 业务字段。
    QString url, filename, savePath;
    if (!extractStringField(obj, QStringLiteral("url"), &url, &err, /*required=*/true)) {
        clientSocket->write(jsonLine("error", err));
        clientSocket->disconnectFromHost();
        return;
    }
    if (!isValidDownloadUrl(url, &err)) {
        qWarning() << "[LocalServer] SSRF/url rejected:" << url << err;
        clientSocket->write(jsonLine("error", QStringLiteral("invalid url: %1").arg(err)));
        clientSocket->disconnectFromHost();
        return;
    }
    if (!extractStringField(obj, QStringLiteral("filename"), &filename, &err, /*required=*/false)) {
        clientSocket->write(jsonLine("error", err));
        clientSocket->disconnectFromHost();
        return;
    }
    if (!filename.isEmpty() && !isSafeFileName(filename, &err)) {
        clientSocket->write(jsonLine("error", QStringLiteral("invalid filename: %1").arg(err)));
        clientSocket->disconnectFromHost();
        return;
    }
    if (!extractStringField(obj, QStringLiteral("savePath"), &savePath, &err, /*required=*/false)) {
        clientSocket->write(jsonLine("error", err));
        clientSocket->disconnectFromHost();
        return;
    }

    LOGD(QString("[LocalServer] new download url=%1 filename=%2").arg(url, filename));
    emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);

    clientSocket->write(jsonLine("success", QStringLiteral("Download request received")));
    clientSocket->disconnectFromHost();
}

/**
 * @brief 处理客户端断开连接
 *
 * 当客户端断开连接时触发，清理客户端套接字对象
 */
void LocalServer::onClientDisconnected()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (clientSocket) {
        // 断开本对象所有信号，避免 deleteLater 期间再有回调进入。
        clientSocket->disconnect(this);
        qDebug() << "Client disconnected:" << clientSocket->peerAddress().toString();
        clientSocket->deleteLater();
    }
}
