#include "httpserver.h"
#include "logger.h"
#include <QDebug>
#include <QHostAddress>
#include <QUrl>
#include <QRegularExpression>
#include <QTcpServer>
#if USE_QHTTPSERVER
#include <QHttpServer>
#include <QHttpServerResponse>
#include <QHttpServerRequest>
#else
#include <QTcpSocket>
#endif

/**
 * @brief HTTP服务器构造函数
 * @param parent 父对象指针
 *
 * 初始化HTTP服务器，用于接收浏览器插件的下载请求
 */
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
#endif

    LOGD("HttpServer初始化完成");
}

/**
 * @brief HTTP服务器析构函数
 *
 * 确保在对象销毁时停止服务器，释放资源
 */
HttpServer::~HttpServer()
{
    stopServer();
}

/**
 * @brief 构造 CORS 预检 / 通用响应头
 * @note 浏览器扩展和本地 Web 页面在跨域 POST 时会先发起 OPTIONS 预检。
 */
static QByteArray corsHeaders()
{
    return QByteArrayLiteral(
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
    );
}

/**
 * @brief 校验下载 URL 是否合法
 *
 * - 必须是 http / https 协议
 * - 拒绝 file:// / ftp:// 等
 * - 拒绝 localhost / 127.0.0.1 / 内网 IP / 云元数据地址 169.254.169.254
 *   （防止本地 SSRF 把下载器变成内网代理）
 */
static bool isValidDownloadUrl(const QString &urlStr)
{
    if (urlStr.isEmpty()) {
        return false;
    }
    QUrl u(urlStr);
    if (!u.isValid()) {
        return false;
    }
    QString scheme = u.scheme().toLower();
    if (scheme != "http" && scheme != "https") {
        return false;
    }
    QString host = u.host().toLower();
    if (host.isEmpty()) {
        return false;
    }
    // 拒绝本地/内网地址
    if (host == "localhost" || host == "127.0.0.1" || host == "::1"
        || host == "0.0.0.0" || host == "169.254.169.254") {
        return false;
    }
    if (host.startsWith("127.") || host.startsWith("10.")
        || host.startsWith("192.168.") || host.startsWith("172.")) {
        return false;
    }
    // 简单粗略检查：公网 IPv4 第一段必须在 1-223（排除多播/保留）
    QRegularExpression ipRe("^(\\d{1,3})\\.");
    auto m = ipRe.match(host);
    if (m.hasMatch()) {
        int first = m.captured(1).toInt();
        if (first >= 224) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 校验 filename / savePath 字段是否包含路径穿越字符
 * 禁止 ..、/、\、以冒号开头（Windows 流式文件名）
 */
static bool isSafeFileName(const QString &name)
{
    if (name.isEmpty()) {
        return true; // 空值表示使用默认路径
    }
    if (name.contains("..") || name.startsWith(":")
        || name.contains('/') || name.contains('\\')) {
        return false;
    }
    return true;
}

/**
 * @brief 启动HTTP服务器
 * @param port 监听端口号
 * @return true 启动成功，false 启动失败
 *
 * 在指定端口启动HTTP服务器监听，只接受本地连接
 * 如果服务器已经在运行，则直接返回成功
 */
bool HttpServer::startServer(quint16 port)
{
    LOGD(QString("开始启动HTTP服务器 - 端口:%1").arg(port));

#if USE_QHTTPSERVER
    // 使用QHttpServer
    try {
        // 主下载入口
        m_httpServer->route("/download", QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest &request) {
                // 解析JSON数据
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(request.body(), &parseError);

                if (parseError.error != QJsonParseError::NoError) {
                    qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
                    QHttpServerResponse resp(
                        QJsonDocument(QJsonObject{{"status", "error"}, {"message", "Invalid JSON"}}).toJson(),
                        "application/json");
                    { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                    return resp;
                }

                if (!doc.isObject()) {
                    QHttpServerResponse resp(
                        QJsonDocument(QJsonObject{{"status", "error"}, {"message", "JSON is not an object"}}).toJson(),
                        "application/json");
                    { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                    return resp;
                }

                QJsonObject obj = doc.object();
                if (!obj.contains("url") || !obj["url"].isString()) {
                    QHttpServerResponse resp(
                        QJsonDocument(QJsonObject{{"status", "error"}, {"message", "Missing or invalid 'url' field"}}).toJson(),
                        "application/json");
                    { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                    return resp;
                }

                QString url = obj["url"].toString();
                QString filename = obj.value("filename").toString(); // filename是可选的
                QString savePath = obj.value("savePath").toString(); // savePath是可选的

                // 安全校验：URL
                if (!isValidDownloadUrl(url)) {
                    qWarning() << "Rejected unsafe URL:" << url;
                    QHttpServerResponse resp(
                        QJsonDocument(QJsonObject{{"status", "error"}, {"message", "Invalid or unsafe URL"}}).toJson(),
                        "application/json");
                    { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                    return resp;
                }
                // 安全校验：filename / savePath 不能含路径穿越
                if (!isSafeFileName(filename) || !isSafeFileName(savePath)) {
                    qWarning() << "Rejected unsafe filename/savePath:" << filename << savePath;
                    QHttpServerResponse resp(
                        QJsonDocument(QJsonObject{{"status", "error"}, {"message", "Invalid filename or savePath"}}).toJson(),
                        "application/json");
                    { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                    return resp;
                }

                qDebug() << "Received new download request. URL:" << url << "Filename:" << filename << "SavePath:" << savePath;
                emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);

                QHttpServerResponse resp(
                    QJsonDocument(QJsonObject{{"status", "success"}, {"message", "Download request received"}}).toJson(),
                    "application/json");
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                return resp;
            });

        // 健康检查端点
        m_httpServer->route("/status", QHttpServerRequest::Method::Get,
            [](const QHttpServerRequest &) {
                QHttpServerResponse resp(
                    QJsonDocument(QJsonObject{{"status", "ok"}}).toJson(),
                    "application/json");
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                return resp;
            });

        // CORS 预检
        m_httpServer->route("/download", QHttpServerRequest::Method::Options,
            [](const QHttpServerRequest &) {
                QHttpServerResponse resp(QByteArray(), "text/plain");
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Methods", "POST, GET, OPTIONS"); resp.setHeaders(hdr); }
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Headers", "Content-Type"); resp.setHeaders(hdr); }
                { QHttpHeaders hdr; hdr.append("Access-Control-Max-Age", "86400"); resp.setHeaders(hdr); }
                return resp;
            });
        m_httpServer->route("/status", QHttpServerRequest::Method::Options,
            [](const QHttpServerRequest &) {
                QHttpServerResponse resp(QByteArray(), "text/plain");
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Origin", "*"); resp.setHeaders(hdr); }
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Methods", "POST, GET, OPTIONS"); resp.setHeaders(hdr); }
                { QHttpHeaders hdr; hdr.append("Access-Control-Allow-Headers", "Content-Type"); resp.setHeaders(hdr); }
                { QHttpHeaders hdr; hdr.append("Access-Control-Max-Age", "86400"); resp.setHeaders(hdr); }
                return resp;
            });

        LOGD("开始监听...");
        // Qt 6.10中的QHttpServer使用bind方法而不是listen方法
        // 创建并绑定QTcpServer
        QTcpServer* tcpServer = new QTcpServer(this);
        bool result = tcpServer->listen(QHostAddress::LocalHost, port);
        LOGD(QString("监听结果:%1").arg(result ? "成功" : "失败"));

        if (!result) {
            LOGD(QString("监听失败 - 错误:%1").arg(tcpServer->errorString()));
            emit error(QString("无法启动服务器: %1").arg(tcpServer->errorString()));
            // 由 Qt 对象树管理，不要双重释放
            return false;
        }

        // 绑定QHttpServer到QTcpServer
        bool bindResult = m_httpServer->bind(tcpServer);
        if (!bindResult) {
            LOGD("绑定QHttpServer失败");
            emit error("无法绑定HTTP服务器");
            // 由 Qt 对象树管理，不要双重释放
            return false;
        }

        // 将tcpServer添加到列表中以便后续管理
        m_tcpServers.append(tcpServer);

        quint16 actualPort = tcpServer->serverPort(); // 获取实际监听的端口

        LOGD(QString("HTTP服务器启动成功 - 监听端口:%1").arg(actualPort));
        emit serverStarted();
        return true;
    } catch (const std::exception& e) {
        LOGD(QString("监听失败 - 错误:%1").arg(e.what()));
        emit error(QString("无法启动服务器: %1").arg(e.what()));
        return false;
    }
#else
    // 使用QTcpServer
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

    LOGD(QString("HTTP服务器启动成功 - 监听端口:%1").arg(port));
    emit serverStarted();
    return true;
#endif
}

void HttpServer::stopServer()
{
#if USE_QHTTPSERVER
    // Qt 6.10中的QHttpServer使用bind方法绑定到QTcpServer
    // 仅关闭监听；tcpServer 由 Qt 对象树管理，无需手动 delete
    if (m_httpServer) {
        for (QTcpServer* server : m_tcpServers) {
            if (server && server->isListening()) {
                server->close();
            }
            // 不再 delete server，避免与对象树重复释放
        }
        m_tcpServers.clear();
        qDebug() << "HTTP server stopped.";
    }
#else
    if (m_tcpServer && m_tcpServer->isListening()) {
        m_tcpServer->close();
        qDebug() << "HTTP server stopped.";
    }
#endif
}

#if !USE_QHTTPSERVER
/**
 * @brief 处理新的客户端连接
 */
void HttpServer::onNewConnection()
{
    QTcpSocket *clientSocket = m_tcpServer->nextPendingConnection();
    if (clientSocket) {
        connect(clientSocket, &QTcpSocket::readyRead, this, &HttpServer::onReadyRead);
        connect(clientSocket, &QTcpSocket::disconnected, this, &HttpServer::onClientDisconnected);
        qDebug() << "New client connected:" << clientSocket->peerAddress().toString();
    }
}

/**
 * @brief 处理客户端发送的数据
 *
 * TCP 不保证一次性收到完整 HTTP 请求，因此每次 readyRead 都把数据追加到
 * 每个 socket 的 m_buffer，按 Content-Length 收齐后再解析。
 */
void HttpServer::onReadyRead()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) {
        return;
    }

    // 按 Qt 推荐使用 QByteArray 增量接收 + Content-Length 判等
    QByteArray pending = clientSocket->readAll();
    qDebug() << "Received data chunk from client, size:" << pending.size();

    // 累积到 buffer
    QByteArray &buf = m_buffers[clientSocket];
    buf.append(pending);

    // 等待 header 结束
    int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return; // 头还没收齐，等下次 readyRead
    }

    // 检查是否需要 body：找 Content-Length（大小写不敏感）
    int contentLength = 0;
    int headerSectionLen = headerEnd;
    int lineStart = 0;
    for (int i = 0; i < headerSectionLen; ++i) {
        if (buf[i] == '\n') {
            QByteArray line = buf.mid(lineStart, i - lineStart);
            // 去掉可能的 \r
            if (line.endsWith('\r')) line.chop(1);
            QByteArray lower = line.toLower();
            if (lower.startsWith("content-length:")) {
                contentLength = line.mid(line.size() - lower.size() + 15).trimmed().toInt();
            }
            lineStart = i + 1;
        }
    }

    // 注意：当前实现不支持 Transfer-Encoding: chunked
    int bodyStart = headerEnd + 4;
    if (buf.size() < bodyStart + contentLength) {
        return; // body 没收齐，等下次 readyRead
    }

    // 数据齐了，处理请求（截取一段，避免重复处理同一请求的累积）
    QByteArray requestData = buf.left(bodyStart + contentLength);
    processHttpRequest(requestData, clientSocket);

    // 处理后清掉 buffer，防止再次进入此分支
    m_buffers.remove(clientSocket);
}

/**
 * @brief 处理客户端断开连接
 */
void HttpServer::onClientDisconnected()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (clientSocket) {
        qDebug() << "Client disconnected:" << clientSocket->peerAddress().toString();
        m_buffers.remove(clientSocket);
        clientSocket->deleteLater();
    }
}

/**
 * @brief 解析HTTP请求并发送响应
 * @param data 已收齐的 HTTP 请求数据（header + body）
 * @param clientSocket 客户端套接字
 */
void HttpServer::processHttpRequest(const QByteArray &data, QTcpSocket *clientSocket)
{
    // 工具 lambda：写入完整 HTTP 响应并安全断开
    auto sendAndClose = [clientSocket](int statusCode, const QByteArray &body, const QByteArray &contentType) {
        QByteArray statusText;
        switch (statusCode) {
            case 200: statusText = "OK"; break;
            case 204: statusText = "No Content"; break;
            case 400: statusText = "Bad Request"; break;
            case 403: statusText = "Forbidden"; break;
            case 404: statusText = "Not Found"; break;
            case 405: statusText = "Method Not Allowed"; break;
            default:  statusText = "OK"; break;
        }
        QByteArray response;
        response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
        response += "Content-Type: " + contentType + "\r\n";
        response += corsHeaders();
        if (!body.isEmpty()) {
            response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        } else {
            response += "Content-Length: 0\r\n";
        }
        response += "Connection: close\r\n\r\n";
        response += body;
        clientSocket->write(response);
        // 等数据真正写出去再断开，避免丢响应
        clientSocket->flush();
        clientSocket->waitForBytesWritten(1000);
        clientSocket->disconnectFromHost();
    };

    // 按 \r\n\r\n 拆 header / body（接受 CRLF，不依赖单独的 \r\n）
    int headerEnd = data.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        sendAndClose(400, "{\"status\":\"error\",\"message\":\"Invalid request\"}", "application/json");
        return;
    }
    QByteArray headerSection = data.left(headerEnd);
    QByteArray bodySection   = data.mid(headerEnd + 4);

    // 按行解析 header
    QList<QByteArray> lines = headerSection.split('\n');
    // 去掉每行末尾可能的 \r
    for (QByteArray &l : lines) {
        if (l.endsWith('\r')) l.chop(1);
    }

    if (lines.isEmpty()) {
        sendAndClose(400, "{\"status\":\"error\",\"message\":\"Empty request\"}", "application/json");
        return;
    }

    QByteArray requestLine = lines[0];
    QList<QByteArray> requestParts = requestLine.split(' ');
    if (requestParts.size() < 3) {
        sendAndClose(400, "{\"status\":\"error\",\"message\":\"Malformed request line\"}", "application/json");
        return;
    }

    QByteArray method = requestParts[0];
    // 去掉 query string: "/download?token=xxx" -> "/download"
    QByteArray rawPath = requestParts[1];
    int qmark = rawPath.indexOf('?');
    QByteArray path = (qmark >= 0) ? rawPath.left(qmark) : rawPath;

    qDebug() << "HTTP Request - Method:" << method << "Path:" << path;

    // 找 Content-Length（大小写不敏感）
    int contentLength = 0;
    for (int i = 1; i < lines.size(); ++i) {
        QByteArray lower = lines[i].toLower();
        if (lower.startsWith("content-length:")) {
            contentLength = lines[i].mid(15).trimmed().toInt();
            break;
        }
    }

    // 注：当前实现不支持 Transfer-Encoding: chunked
    // 如需支持，需解析 chunk-size CRLF chunk-data CRLF ... 0 CRLF CRLF

    // OPTIONS 预检
    if (method == "OPTIONS" && (path == "/download" || path == "/status")) {
        sendAndClose(204, QByteArray(), "text/plain");
        return;
    }

    // 健康检查
    if (method == "GET" && path == "/status") {
        sendAndClose(200, "{\"status\":\"ok\"}", "application/json");
        return;
    }

    // 根路径
    if (method == "GET" && path == "/") {
        sendAndClose(200, "Downloader HTTP Server is running", "text/plain");
        return;
    }

    // 主下载入口
    if (method == "POST" && path == "/download") {
        QByteArray body = bodySection.left(contentLength);

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
            sendAndClose(400, "{\"status\":\"error\",\"message\":\"Invalid JSON\"}", "application/json");
            return;
        }
        if (!doc.isObject()) {
            sendAndClose(400, "{\"status\":\"error\",\"message\":\"JSON is not an object\"}", "application/json");
            return;
        }
        QJsonObject obj = doc.object();
        if (!obj.contains("url") || !obj["url"].isString()) {
            sendAndClose(400, "{\"status\":\"error\",\"message\":\"Missing or invalid 'url' field\"}", "application/json");
            return;
        }

        QString url = obj["url"].toString();
        QString filename = obj.value("filename").toString();
        QString savePath = obj.value("savePath").toString();

        // 安全校验
        if (!isValidDownloadUrl(url)) {
            qWarning() << "Rejected unsafe URL:" << url;
            sendAndClose(403, "{\"status\":\"error\",\"message\":\"Invalid or unsafe URL\"}", "application/json");
            return;
        }
        if (!isSafeFileName(filename) || !isSafeFileName(savePath)) {
            qWarning() << "Rejected unsafe filename/savePath:" << filename << savePath;
            sendAndClose(400, "{\"status\":\"error\",\"message\":\"Invalid filename or savePath\"}", "application/json");
            return;
        }

        qDebug() << "Received new download request. URL:" << url << "Filename:" << filename << "SavePath:" << savePath;
        emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);
        sendAndClose(200, "{\"status\":\"success\",\"message\":\"Download request received\"}", "application/json");
        return;
    }

    // 其它路径 / 方法 -> 404 / 405
    if (path == "/download" || path == "/status") {
        sendAndClose(405, "{\"status\":\"error\",\"message\":\"Method not allowed\"}", "application/json");
    } else {
        sendAndClose(404, "{\"status\":\"error\",\"message\":\"Not found\"}", "application/json");
    }
}
#endif