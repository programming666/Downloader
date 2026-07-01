#include "httpserver.h"
#include "logger.h"
#include <QDebug>
#include <QHostAddress>
#include <QHostInfo>
#include <QUrl>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QTcpServer>
#include <QPointer>

#if USE_QHTTPSERVER
#include <QHttpServer>
#include <QHttpServerResponse>
#include <QHttpServerRequest>
#include <QHttpHeaders>
#endif

namespace {

// TODO(integration): integrate with SettingsManager::bearerToken() once the
// persistence agent adds that API. Until then, we hardcode a process-local
// token to block CSRF for any external web page that can reach
// http://localhost:<port>/download.
// 在 SettingsManager 暴露 bearerToken() 之前，先硬编码一份进程级 token
// 用于浏览器插件与本地 HTTP 服务之间的鉴权，规避 CSRF。
static const QByteArray HARDCODED_BEARER_TOKEN =
    QByteArrayLiteral("DownloaderLocal-bearer-9c5f2a1e3b7d4f60");

// 允许跨域来源白名单：仅信任 Chrome/Edge 扩展 manifest 的 origin。
static const QSet<QByteArray> kAllowedOrigins = {
    QByteArrayLiteral("chrome-extension://"), // 后面接真实 extension id（按需收紧）
    QByteArrayLiteral("edge-extension://"),
};

static bool originAllowed(const QByteArray &origin)
{
    if (origin.isEmpty()) return false;
    for (const QByteArray &prefix : kAllowedOrigins) {
        if (origin.startsWith(prefix)) {
            // 拒绝空 id / 仅前缀
            if (origin.size() <= prefix.size() + 1) return false;
            return true;
        }
    }
    return false;
}

static QByteArray bearerFromAuthHeader(const QByteArray &authHeader)
{
    // 支持 "Bearer <token>"，大小写不敏感。
    static const QByteArray kBearerPrefix = "Bearer ";
    if (authHeader.size() <= kBearerPrefix.size()) return {};
    if (authHeader.left(kBearerPrefix.size()).toLower() != kBearerPrefix.toLower()) return {};
    return authHeader.mid(kBearerPrefix.size()).trimmed();
}

// 1) URL/SSRF 校验
// ---------------------------------------------------------------
static bool isPrivateOrLoopbackIp(const QHostAddress &addr)
{
    if (addr.isLoopback())  return true;
    if (addr.isLinkLocal()) return true;        // 169.254/16, fe80::/10
    if (addr.isMulticast()) return true;

    // 协议相关范围检查（QHostAddress 不会全部覆盖）
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = addr.toIPv4Address();
        const quint32 b1 = (ip >> 24) & 0xFFu;
        const quint32 b2 = (ip >> 16) & 0xFFu;
        // 0.0.0.0/8 — "this network"
        if (b1 == 0u) return true;
        // 10.0.0.0/8
        if (b1 == 10u) return true;
        // 100.64.0.0/10 — CGNAT
        if (b1 == 100u && b2 >= 64u && b2 <= 127u) return true;
        // 127.0.0.0/8 — loopback（QHostAddress 已部分覆盖）
        if (b1 == 127u) return true;
        // 169.254.0.0/16 — link-local（QHostAddress 已覆盖）
        // 172.16.0.0/12
        if (b1 == 172u && b2 >= 16u && b2 <= 31u) return true;
        // 192.168.0.0/16
        if (b1 == 192u && b2 == 168u) return true;
        // 192.0.2.0/24, 198.51.100.0/24, 203.0.113.0/24 — TEST-NET
        if ((b1 == 192u && b2 == 0u  && ((ip >> 8) & 0xFFu) == 2u)  ||
            (b1 == 198u && b2 == 51u && ((ip >> 8) & 0xFFu) == 100u) ||
            (b1 == 203u && b2 == 0u  && ((ip >> 8) & 0xFFu) == 113u))
            return true;
        // 198.18.0.0/15 — benchmarking
        if (b1 == 198u && (b2 == 18u || b2 == 19u)) return true;
        // 240.0.0.0/4 — reserved (含 255.255.255.255 broadcast)
        if (b1 >= 240u) return true;
        return false;
    }

    // IPv6
    if (addr.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR ip6 = addr.toIPv6Address();
        // fc00::/7 — unique local
        if ((ip6[0] & 0xFEu) == 0xFCu) return true;
        // fe80::/10 — link-local（QHostAddress 已覆盖）
        // ff00::/8 — multicast（QHostAddress 已覆盖）
        // ::ffff:a.b.c.d — IPv4-mapped：递归检查嵌入的 IPv4
        if (addr.isIPv4Mapped()) {
            const quint32 mapped = (quint32(ip6[12]) << 24) | (quint32(ip6[13]) << 16)
                                 | (quint32(ip6[14]) << 8)  |  quint32(ip6[15]);
            const QHostAddress v4(mapped);
            return isPrivateOrLoopbackIp(v4);
        }
        // 其它 IPv6 未指定/loopback(::1) 由 QHostAddress 覆盖
        return false;
    }
    return true; // 未知协议视为不安全
}

// 同步解析主机名，3 秒超时；任一解析结果落在私有/回环段则拒绝。
static bool hostResolvesToPublicAddress(const QString &host, QString *errorOut)
{
    if (host.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("empty host");
        return false;
    }

    // 字面 IP 路径
    QHostAddress literal;
    if (literal.setAddress(host)) {
        if (isPrivateOrLoopbackIp(literal)) {
            if (errorOut) *errorOut = QStringLiteral("host resolves to private/loopback address");
            return false;
        }
        return true;
    }

    // 主机名 → DNS 解析（3 秒超时）
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

// 2) 文件名校验
// ---------------------------------------------------------------
static bool isReservedWindowsName(const QString &base)
{
    // 不带扩展名的基础名。匹配 CON/PRN/AUX/NUL/COM1-9/LPT1-9（大小写不敏感）。
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
    // Windows 路径分隔符与保留字符
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
    // 取 basename（去掉 Windows 盘符 + 路径分隔）
    QString basename = name;
    const int lastSlash = qMax(basename.lastIndexOf('/'), basename.lastIndexOf('\\'));
    if (lastSlash >= 0) basename = basename.mid(lastSlash + 1);
    // 拒绝前导 Windows 盘符（"C:" / "C:foo"）
    if (basename.size() >= 2 && basename[1] == QLatin1Char(':') &&
        ((basename[0] >= QLatin1Char('A') && basename[0] <= QLatin1Char('Z')) ||
         (basename[0] >= QLatin1Char('a') && basename[0] <= QLatin1Char('z')))) {
        if (errorOut) *errorOut = QStringLiteral("drive letter not allowed");
        return false;
    }
    // 拒绝以 '.' 或 ' ' 结尾
    if (basename.endsWith(QLatin1Char('.')) || basename.endsWith(QLatin1Char(' '))) {
        if (errorOut) *errorOut = QStringLiteral("filename must not end with '.' or space");
        return false;
    }
    // basename 不应包含 '.'（剥离扩展名后再判断）
    QString stem = basename.section(QLatin1Char('.'), 0, 0);
    if (isReservedWindowsName(stem)) {
        if (errorOut) *errorOut = QStringLiteral("reserved Windows device name");
        return false;
    }
    // 拒绝纯 ".." 或 "." 路径段
    if (basename == QStringLiteral(".") || basename == QStringLiteral("..")) {
        if (errorOut) *errorOut = QStringLiteral("invalid path segment");
        return false;
    }
    return true;
}

// 3) JSON 字段严格校验
// ---------------------------------------------------------------
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

// 4) 构造标准 HTTP/1.1 响应
// ---------------------------------------------------------------
static QByteArray buildHttpResponse(int statusCode,
                                    const QByteArray &reason,
                                    const QByteArray &body,
                                    const QByteArray &contentType,
                                    const QByteArray &origin)
{
    QByteArray resp;
    resp.reserve(256 + body.size());
    resp += "HTTP/1.1 ";
    resp += QByteArray::number(statusCode);
    resp += ' ';
    resp += reason;
    resp += "\r\n";
    resp += "Content-Type: ";
    resp += contentType;
    resp += "\r\n";
    resp += "Content-Length: ";
    resp += QByteArray::number(body.size());
    resp += "\r\n";
    resp += "Connection: close\r\n";
    if (!origin.isEmpty()) {
        resp += "Access-Control-Allow-Origin: ";
        resp += origin;
        resp += "\r\n";
        resp += "Vary: Origin\r\n";
    }
    // 短缓存 CORS preflight 响应，避免长期允许宽松的跨域缓存
    resp += "Access-Control-Max-Age: 300\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
}

static QByteArray jsonErrorBody(const QString &message)
{
    QJsonObject o;
    o.insert(QStringLiteral("status"),  QStringLiteral("error"));
    o.insert(QStringLiteral("message"), message);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

static QByteArray jsonOkBody(const QString &message)
{
    QJsonObject o;
    o.insert(QStringLiteral("status"),  QStringLiteral("success"));
    o.insert(QStringLiteral("message"), message);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

} // namespace

// =========================================================================
// 构造/析构
// =========================================================================
HttpServer::HttpServer(QObject *parent)
    : QObject(parent)
{
    LOGD("开始初始化HttpServer");

#if USE_QHTTPSERVER
    LOGD("使用QHttpServer...");
    m_httpServer = new QHttpServer(this);
#else
    LOGD("使用QTcpServer...");
    m_tcpServer = new QTcpServer(this);
    LOGD("QTcpServer创建完成");

    LOGD("开始连接信号...");
    connect(m_tcpServer, &QTcpServer::newConnection, this, &HttpServer::onNewConnection);
    LOGD("信号连接完成");

    // Slowloris 防御：限制挂起连接上限。
    m_tcpServer->setMaxPendingConnections(MAX_PENDING_CONNS);
#endif

    LOGD("HttpServer初始化完成");
}

HttpServer::~HttpServer()
{
    stopServer();
}

// =========================================================================
// startServer / stopServer
// =========================================================================
bool HttpServer::startServer(quint16 port)
{
    LOGD(QString("开始启动HTTP服务器 - 端口:%1").arg(port));

    // 端口绑定竞态修复：每次启动前先 stopServer，避免重复绑定时旧 socket 残留。
    stopServer();

#if USE_QHTTPSERVER
    // QHttpServer 路径
    try {
        m_httpServer->route("/download", QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest &request) -> QHttpServerResponse {
                // 1) Origin 校验
                const QByteArray origin = request.value("Origin").toUtf8();
                const bool originOk = originAllowed(origin);
                if (!originOk) {
                    qWarning() << "[HttpServer] rejected origin:" << origin;
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::Forbidden,
                        jsonErrorBody(QStringLiteral("origin not allowed")),
                        "application/json");
                }

                // 2) Bearer token 校验
                const QByteArray auth = request.value("Authorization").toUtf8();
                const QByteArray presented = bearerFromAuthHeader(auth);
                if (presented.isEmpty() || presented != HARDCODED_BEARER_TOKEN) {
                    qWarning() << "[HttpServer] rejected: missing/invalid bearer token";
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::Unauthorized,
                        jsonErrorBody(QStringLiteral("missing or invalid bearer token")),
                        "application/json");
                }

                // 3) 限制 body 大小
                const QByteArray body = request.body();
                if (body.size() > MAX_BODY_SIZE) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::PayloadTooLarge,
                        jsonErrorBody(QStringLiteral("request body too large")),
                        "application/json");
                }

                // 4) 解析 JSON
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
                if (parseError.error != QJsonParseError::NoError) {
                    qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(QStringLiteral("Invalid JSON")),
                        "application/json");
                }
                if (!doc.isObject()) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(QStringLiteral("JSON is not an object")),
                        "application/json");
                }

                QJsonObject obj = doc.object();
                QString url, filename, savePath, err;
                if (!extractStringField(obj, "url", &url, &err, /*required=*/true)) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(err),
                        "application/json");
                }
                if (!isValidDownloadUrl(url, &err)) {
                    qWarning() << "[HttpServer] SSRF/url rejected:" << url << err;
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(QStringLiteral("invalid url: %1").arg(err)),
                        "application/json");
                }
                if (!extractStringField(obj, "filename", &filename, &err, /*required=*/false)) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(err),
                        "application/json");
                }
                if (!extractStringField(obj, "savePath", &savePath, &err, /*required=*/false)) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(err),
                        "application/json");
                }
                if (!filename.isEmpty() && !isSafeFileName(filename, &err)) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::BadRequest,
                        jsonErrorBody(QStringLiteral("invalid filename: %1").arg(err)),
                        "application/json");
                }

                LOGD(QString("[HttpServer] new download url=%1 filename=%2").arg(url, filename));
                emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);

                return QHttpServerResponse(
                    jsonOkBody(QStringLiteral("Download request received")),
                    "application/json");
            });

        // 共享 CORS preflight：仅在允许的 origin 上回显。其它 origin 直接拒绝。
        // 注：Qt 6.10 的 QHttpServerResponse 不暴露 addHeader/setHeader，只能用
        // setHeaders(QHttpHeaders) 整体替换（6.8+）。这里直接构造一个带 Origin
        // header 的响应即可，preflight 浏览器会接受。
        m_httpServer->route("/download", QHttpServerRequest::Method::Options,
            [](const QHttpServerRequest &request) {
                const QByteArray origin = request.value("Origin").toUtf8();
                if (!originAllowed(origin)) {
                    return QHttpServerResponse(
                        QHttpServerResponse::StatusCode::Forbidden,
                        QByteArrayLiteral("origin not allowed"),
                        "text/plain");
                }
                // 用空 body + NoContent 状态；CORS 头由浏览器扩展使用
                // Access-Control-Request-Headers 自行判断（我们要求 Authorization）。
                QHttpServerResponse resp(
                    QHttpServerResponse::StatusCode::NoContent);
                QHttpHeaders hdrs;
                hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,  QString::fromUtf8(origin));
                hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods, QStringLiteral("POST, OPTIONS"));
                hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders, QStringLiteral("Authorization, Content-Type"));
                hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlMaxAge,        QStringLiteral("300"));
                hdrs.append(QHttpHeaders::WellKnownHeader::Vary,                       QStringLiteral("Origin"));
                resp.setHeaders(hdrs);
                return resp;
            });

        LOGD("开始监听...");
        QTcpServer* tcpServer = new QTcpServer(this);
        bool result = tcpServer->listen(QHostAddress::LocalHost, port);
        LOGD(QString("监听结果:%1").arg(result ? "成功" : "失败"));

        if (!result) {
            LOGD(QString("监听失败 - 错误:%1").arg(tcpServer->errorString()));
            emit error(QString("无法启动服务器: %1").arg(tcpServer->errorString()));
            delete tcpServer;
            return false;
        }

        bool bindResult = m_httpServer->bind(tcpServer);
        if (!bindResult) {
            LOGD("绑定QHttpServer失败");
            emit error("无法绑定HTTP服务器");
            delete tcpServer;
            return false;
        }

        m_listenServer = tcpServer;
        const quint16 actualPort = tcpServer->serverPort();

        LOGD(QString("HTTP服务器启动成功 - 监听端口:%1").arg(actualPort));
        emit serverStarted();
        return true;
    } catch (const std::exception& e) {
        LOGD(QString("监听失败 - 错误:%1").arg(e.what()));
        emit error(QString("无法启动服务器: %1").arg(e.what()));
        return false;
    }
#else
    // QTcpServer fallback
    LOGD("开始监听...");
    bool result = m_tcpServer->listen(QHostAddress::LocalHost, port);
    LOGD(QString("监听结果:%1").arg(result ? "成功" : "失败"));

    if (!result) {
        LOGD(QString("监听失败 - 错误:%1").arg(m_tcpServer->errorString()));
        emit error(QString("无法启动服务器: %1").arg(m_tcpServer->errorString()));
        return false;
    }

    LOGD(QString("HTTP服务器启动成功 - 监听端口:%1").arg(port));
    emit serverStarted();
    return true;
#endif
}

void HttpServer::stopServer()
{
#if USE_QHTTPSERVER
    if (m_httpServer) {
        // Qt 6.10 中 QHttpServer 通过 bind(QTcpServer*) 持有监听 socket；
        // 我们释放自己创建的那个 QTcpServer 即可断开监听。
        if (m_listenServer) {
            if (m_listenServer->isListening()) {
                m_listenServer->close();
            }
            delete m_listenServer;
            m_listenServer = nullptr;
        }
        qDebug() << "HTTP server stopped.";
    }
#else
    if (m_tcpServer && m_tcpServer->isListening()) {
        m_tcpServer->close();
        qDebug() << "HTTP server stopped.";
    }
    // 清理残余缓冲/定时器：服务停止时丢弃所有未完成连接。
    const auto sockets = m_buffers.keys();
    for (QTcpSocket *s : sockets) {
        if (s) {
            QTimer *t = m_idleTimers.value(s, nullptr);
            if (t) { t->stop(); t->deleteLater(); }
            s->disconnect(this);
            if (s->state() != QAbstractSocket::UnconnectedState) {
                s->abort();
            }
            s->deleteLater();
        }
    }
    m_buffers.clear();
    m_idleTimers.clear();
#endif
}

// =========================================================================
// QTcpServer fallback 实现
// =========================================================================
#if !USE_QHTTPSERVER

void HttpServer::onNewConnection()
{
    while (QTcpSocket *clientSocket = m_tcpServer->nextPendingConnection()) {
        // 让 socket 跟随本对象的父子体系，避免 dangling key。
        clientSocket->setParent(this);

        connect(clientSocket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(clientSocket, &QTcpSocket::disconnected, this, &HttpServer::onClientDisconnected);

        // 慢速客户端 30s 空闲超时
        QTimer *idleTimer = new QTimer(clientSocket);
        idleTimer->setSingleShot(true);
        idleTimer->setInterval(IDLE_TIMEOUT_MS);
        connect(idleTimer, &QTimer::timeout, this, [this, clientSocket]() {
            qWarning() << "[HttpServer] idle timeout, dropping peer:" << clientSocket->peerAddress().toString();
            clientSocket->abort();
            clientSocket->deleteLater();
        });
        idleTimer->start();
        m_idleTimers.insert(clientSocket, idleTimer);

        m_buffers.insert(clientSocket, QByteArray());
    }
}

void HttpServer::onReadyRead()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) return;

    QByteArray chunk = clientSocket->readAll();
    if (chunk.isEmpty()) return;

    QByteArray &buf = m_buffers[clientSocket];
    if (buf.size() + chunk.size() > MAX_BUFFER_SIZE) {
        // 超额缓冲，强制断开防 OOM
        qWarning() << "[HttpServer] client buffer exceeded, dropping peer";
        clientSocket->abort();
        clientSocket->deleteLater();
        return;
    }
    buf.append(chunk);

    // 重置空闲超时
    QTimer *idleTimer = m_idleTimers.value(clientSocket, nullptr);
    if (idleTimer) idleTimer->start();

    // 尝试解析一个或多个完整 HTTP 请求（pipelining）。
    // 每次循环消费一个完整请求；剩余字节保留供下一次循环处理。
    while (true) {
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) return; // 头部还不完整，等下一波数据

        // 解析 Content-Length；空 body 或非 POST 也算合法（contentLength=0）。
        int contentLength = 0;
        bool gotContentLength = false;
        const QList<QByteArray> headerLines = buf.left(headerEnd).split('\n');
        for (const QByteArray &rawLine : headerLines) {
            const QByteArray line = rawLine.trimmed();
            const int colon = line.indexOf(':');
            if (colon < 0) continue;
            const QByteArray name = line.left(colon).trimmed().toLower();
            if (name != "content-length") continue;
            bool ok = false;
            int n = line.mid(colon + 1).trimmed().toInt(&ok);
            if (!ok || n < 0 || n > MAX_BODY_SIZE) {
                // Content-Length 非法或过大：拒绝
                QByteArray resp = buildHttpResponse(413, "Payload Too Large",
                    jsonErrorBody(QStringLiteral("Content-Length out of range")),
                    "application/json", QByteArray());
                clientSocket->write(resp);
                QTimer::singleShot(CLOSE_GRACE_MS, clientSocket, [clientSocket]() {
                    clientSocket->disconnectFromHost();
                });
                m_buffers.remove(clientSocket);
                return;
            }
            contentLength = n;
            gotContentLength = true;
        }
        (void)gotContentLength;

        const int bodyStart = headerEnd + 4;
        const int totalLen   = bodyStart + contentLength;
        if (buf.size() < totalLen) return; // body 不完整，等更多数据

        // 解析一次请求；用 QPointer 守卫自己与 socket。
        QPointer<HttpServer> safeThis(this);
        QPointer<QTcpSocket> safeSocket(clientSocket);
        const bool keepGoing = processHttpRequest(clientSocket);

        if (!safeThis || !safeSocket) return;
        // processHttpRequest 可能因错误路径清理了 buffer；检查后再继续。
        if (!m_buffers.contains(safeSocket.data())) return;

        if (!keepGoing) {
            // 响应要求关闭连接；停止 pipeline 处理，关闭前清空 buffer。
            m_buffers[safeSocket.data()].clear();
            return;
        }

        // 消费本次请求的字节，保留剩余尾部供后续循环处理。
        QByteArray &b = m_buffers[safeSocket.data()];
        if (totalLen >= b.size()) {
            b.clear();
            return;
        }
        b = b.mid(totalLen);
        if (b.isEmpty()) return;
    }
}

void HttpServer::onClientDisconnected()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) return;

    qDebug() << "Client disconnected:" << clientSocket->peerAddress().toString();

    // 关键：先断开本对象所有信号，避免 deleteLater 期间再有回调进入。
    clientSocket->disconnect(this);

    QTimer *idleTimer = m_idleTimers.value(clientSocket, nullptr);
    if (idleTimer) {
        idleTimer->stop();
        idleTimer->deleteLater();
    }
    m_idleTimers.remove(clientSocket);

    m_buffers.remove(clientSocket);
    clientSocket->deleteLater();
}

// 写响应并根据 closeAfter 决定是否调度断开连接。
// 返回 false 表示应当停止后续 pipeline 处理（即将关闭）。
static bool writeAndMaybeClose(QTcpSocket *clientSocket,
                               const QByteArray &response,
                               bool closeAfter)
{
    if (!clientSocket) return false;
    clientSocket->write(response);
    if (closeAfter) {
        QPointer<QTcpSocket> safe(clientSocket);
        QTimer::singleShot(CLOSE_GRACE_MS, clientSocket, [safe]() {
            if (safe && safe->state() == QAbstractSocket::ConnectedState) {
                safe->disconnectFromHost();
            }
        });
        return false;
    }
    return true;
}

bool HttpServer::processHttpRequest(QTcpSocket *clientSocket)
{
    if (!clientSocket) return false;
    auto bufIt = m_buffers.find(clientSocket);
    if (bufIt == m_buffers.end()) return false;
    QByteArray &buf = *bufIt;
    if (buf.isEmpty()) return false;

    // 提取请求行 + 头
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;
    const QByteArray headersBlock = buf.left(headerEnd);
    const QList<QByteArray> headerLines = headersBlock.split('\n');
    if (headerLines.isEmpty()) {
        buf.clear();
        return false;
    }

    const QByteArray requestLine = headerLines[0].trimmed();
    const QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 3) {
        return writeAndMaybeClose(clientSocket,
            buildHttpResponse(400, "Bad Request",
                jsonErrorBody(QStringLiteral("Malformed request line")),
                "application/json", QByteArray()),
            /*closeAfter=*/true);
    }
    const QByteArray method = requestParts[0];
    const QByteArray path   = requestParts[1];

    LOGD(QString("HTTP Request - Method:%1 Path:%2").arg(QString::fromUtf8(method), QString::fromUtf8(path)));

    // 抽取 Origin / Authorization / Content-Length
    QByteArray origin, authHeader;
    int contentLength = 0;
    bool gotContentLength = false;
    for (const QByteArray &rawLine : headerLines) {
        const QByteArray line = rawLine.trimmed();
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QByteArray name  = line.left(colon).trimmed();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name.compare("Origin", Qt::CaseInsensitive) == 0) {
            origin = value;
        } else if (name.compare("Authorization", Qt::CaseInsensitive) == 0) {
            authHeader = value;
        } else if (name.compare("Content-Length", Qt::CaseInsensitive) == 0) {
            bool ok = false;
            int n = value.toInt(&ok);
            if (!ok || n < 0 || n > MAX_BODY_SIZE) {
                return writeAndMaybeClose(clientSocket,
                    buildHttpResponse(413, "Payload Too Large",
                        jsonErrorBody(QStringLiteral("Content-Length out of range")),
                        "application/json", origin),
                    /*closeAfter=*/true);
            }
            contentLength = n;
            gotContentLength = true;
        }
    }

    // 仅处理 POST /download 与 GET / 与 OPTIONS
    if (method == "POST" && path == "/download") {
        // 1) Origin
        if (!originAllowed(origin)) {
            qWarning() << "[HttpServer] rejected origin:" << origin;
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(403, "Forbidden",
                    jsonErrorBody(QStringLiteral("origin not allowed")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }

        // 2) Bearer token
        const QByteArray presented = bearerFromAuthHeader(authHeader);
        if (presented.isEmpty() || presented != HARDCODED_BEARER_TOKEN) {
            qWarning() << "[HttpServer] rejected: missing/invalid bearer token";
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(401, "Unauthorized",
                    jsonErrorBody(QStringLiteral("missing or invalid bearer token")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }

        // 3) body 完整性检查
        if (!gotContentLength) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(QStringLiteral("missing Content-Length")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }
        const int bodyStart = headerEnd + 4;
        if (buf.size() < bodyStart + contentLength) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(QStringLiteral("body shorter than Content-Length")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }
        QByteArray body = buf.mid(bodyStart, contentLength);

        // 4) JSON 解析
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(QStringLiteral("Invalid JSON")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }
        if (!doc.isObject()) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(QStringLiteral("JSON is not an object")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }

        QJsonObject obj = doc.object();
        QString url, filename, savePath, err;
        if (!extractStringField(obj, "url", &url, &err, true)) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(err), "application/json", origin),
                /*closeAfter=*/true);
        }
        if (!isValidDownloadUrl(url, &err)) {
            qWarning() << "[HttpServer] SSRF/url rejected:" << url << err;
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(QStringLiteral("invalid url: %1").arg(err)),
                    "application/json", origin),
                /*closeAfter=*/true);
        }
        if (!extractStringField(obj, "filename", &filename, &err, false)) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(err), "application/json", origin),
                /*closeAfter=*/true);
        }
        if (!filename.isEmpty() && !isSafeFileName(filename, &err)) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(QStringLiteral("invalid filename: %1").arg(err)),
                    "application/json", origin),
                /*closeAfter=*/true);
        }
        if (!extractStringField(obj, "savePath", &savePath, &err, false)) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(400, "Bad Request",
                    jsonErrorBody(err), "application/json", origin),
                /*closeAfter=*/true);
        }

        LOGD(QString("[HttpServer] new download url=%1 filename=%2").arg(url, filename));
        emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);

        // 成功响应后关闭连接（每个浏览器插件请求都是独立 connection）
        return writeAndMaybeClose(clientSocket,
            buildHttpResponse(200, "OK",
                jsonOkBody(QStringLiteral("Download request received")),
                "application/json", origin),
            /*closeAfter=*/true);
    } else if (method == "GET" && path == "/") {
        const QByteArray body = "Downloader HTTP Server is running";
        return writeAndMaybeClose(clientSocket,
            buildHttpResponse(200, "OK", body, "text/plain", origin),
            /*closeAfter=*/true);
    } else if (method == "OPTIONS") {
        // CORS preflight
        if (!originAllowed(origin)) {
            return writeAndMaybeClose(clientSocket,
                buildHttpResponse(403, "Forbidden",
                    jsonErrorBody(QStringLiteral("origin not allowed")),
                    "application/json", origin),
                /*closeAfter=*/true);
        }
        QByteArray headers;
        headers += "HTTP/1.1 204 No Content\r\n";
        headers += "Access-Control-Allow-Origin: "  + origin + "\r\n";
        headers += "Access-Control-Allow-Methods: POST, OPTIONS\r\n";
        headers += "Access-Control-Allow-Headers: Authorization, Content-Type\r\n";
        headers += "Access-Control-Max-Age: 300\r\n";
        headers += "Vary: Origin\r\n";
        headers += "Content-Length: 0\r\n";
        headers += "Connection: close\r\n\r\n";
        return writeAndMaybeClose(clientSocket, headers, /*closeAfter=*/true);
    } else if (method != "POST" && method != "GET" && method != "OPTIONS") {
        return writeAndMaybeClose(clientSocket,
            buildHttpResponse(405, "Method Not Allowed",
                jsonErrorBody(QStringLiteral("method not allowed")),
                "application/json", origin),
            /*closeAfter=*/true);
    } else {
        return writeAndMaybeClose(clientSocket,
            buildHttpResponse(404, "Not Found",
                jsonErrorBody(QStringLiteral("not found")),
                "application/json", origin),
            /*closeAfter=*/true);
    }
}

#endif // !USE_QHTTPSERVER