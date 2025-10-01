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
    // 先检查服务器是否已经在监听
    // 注意：Qt 6.10中的QHttpServer可能没有isListening()方法，我们直接尝试停止
    try {
        m_httpServer->route("/download", QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest &request) {
                // 解析JSON数据
                QJsonParseError parseError;
                QJsonDocument doc = QJsonDocument::fromJson(request.body(), &parseError);

                if (parseError.error != QJsonParseError::NoError) {
                    qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
                    return QHttpServerResponse(QJsonDocument(QJsonObject{{"status", "error"}, {"message", "Invalid JSON"}}).toJson(), "application/json");
                }
                
                if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("url") && obj["url"].isString()) {
                        QString url = obj["url"].toString();
                        QString filename = obj.value("filename").toString(); // filename是可选的
                        QString savePath = obj.value("savePath").toString(); // savePath是可选的

                        qDebug() << "Received new download request. URL:" << url << "Filename:" << filename << "SavePath:" << savePath;
                        emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);

                        // 回复客户端表示成功
                        return QHttpServerResponse(QJsonDocument(QJsonObject{{"status", "success"}, {"message", "Download request received"}}).toJson(), "application/json");
                    } else {
                        return QHttpServerResponse(QJsonDocument(QJsonObject{{"status", "error"}, {"message", "Missing or invalid 'url' field"}}).toJson(), "application/json");
                    }
                } else {
                    return QHttpServerResponse(QJsonDocument(QJsonObject{{"status", "error"}, {"message", "JSON is not an object"}}).toJson(), "application/json");
                }
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
            delete tcpServer;
            return false;
        }
        
        // 绑定QHttpServer到QTcpServer
        bool bindResult = m_httpServer->bind(tcpServer);
        if (!bindResult) {
            LOGD("绑定QHttpServer失败");
            emit error("无法绑定HTTP服务器");
            delete tcpServer;
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
    // 我们需要停止并删除所有我们创建的服务器
    if (m_httpServer) {
        for (QTcpServer* server : m_tcpServers) {
            if (server->isListening()) {
                server->close();
            }
            // 删除我们创建的服务器
            delete server;
        }
        m_tcpServers.clear();
        qDebug() << "HTTP server stopped.";
    }
#else
    if (m_tcpServer->isListening()) {
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
 */
void HttpServer::onReadyRead()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) {
        return;
    }

    QByteArray data = clientSocket->readAll();
    qDebug() << "Received data from client:" << data;

    // 处理HTTP请求
    processHttpRequest(data, clientSocket);
}

/**
 * @brief 处理客户端断开连接
 */
void HttpServer::onClientDisconnected()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (clientSocket) {
        qDebug() << "Client disconnected:" << clientSocket->peerAddress().toString();
        clientSocket->deleteLater();
    }
}

/**
 * @brief 解析HTTP请求并发送响应
 * @param data HTTP请求数据
 * @param clientSocket 客户端套接字
 */
void HttpServer::processHttpRequest(const QByteArray &data, QTcpSocket *clientSocket)
{
    // 简单解析HTTP请求
    QString request = QString::fromUtf8(data);
    QStringList lines = request.split("\r\n");
    
    if (lines.isEmpty()) {
        // 发送错误响应
        QString response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        clientSocket->write(response.toUtf8());
        clientSocket->disconnectFromHost();
        return;
    }
    
    // 解析请求行
    QString requestLine = lines[0];
    QStringList requestParts = requestLine.split(" ");
    if (requestParts.size() < 3) {
        // 发送错误响应
        QString response = "HTTP/1.1 400 Bad Request\r\n\r\n";
        clientSocket->write(response.toUtf8());
        clientSocket->disconnectFromHost();
        return;
    }
    
    QString method = requestParts[0];
    QString path = requestParts[1];
    
    qDebug() << "HTTP Request - Method:" << method << "Path:" << path;
    
    // 只处理POST到/download路径的请求
    if (method == "POST" && path == "/download") {
        // 查找Content-Length头部
        int contentLength = 0;
        for (int i = 1; i < lines.size(); ++i) {
            if (lines[i].startsWith("Content-Length:")) {
                contentLength = lines[i].split(":")[1].trimmed().toInt();
                break;
            }
        }
        
        // 查找请求体
        int bodyStart = request.indexOf("\r\n\r\n");
        if (bodyStart != -1) {
            bodyStart += 4; // 跳过\r\n\r\n
            QByteArray body = data.mid(bodyStart, contentLength);
            
            // 解析JSON数据
            QJsonParseError parseError;
            QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

            if (parseError.error != QJsonParseError::NoError) {
                qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
                QString response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"status\":\"error\", \"message\":\"Invalid JSON\"}";
                clientSocket->write(response.toUtf8());
                clientSocket->disconnectFromHost();
                return;
            }
            
            if (doc.isObject()) {
                    QJsonObject obj = doc.object();
                    if (obj.contains("url") && obj["url"].isString()) {
                        QString url = obj["url"].toString();
                        QString filename = obj.value("filename").toString(); // filename是可选的
                        QString savePath = obj.value("savePath").toString(); // savePath是可选的

                        qDebug() << "Received new download request. URL:" << url << "Filename:" << filename << "SavePath:" << savePath;
                        emit newDownloadRequest(url, savePath.isEmpty() ? filename : savePath);

                        // 回复客户端表示成功
                        QString response = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"status\":\"success\", \"message\":\"Download request received\"}";
                        clientSocket->write(response.toUtf8());
                    } else {
                        QString response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"status\":\"error\", \"message\":\"Missing or invalid 'url' field\"}";
                        clientSocket->write(response.toUtf8());
                    }
                } else {
                QString response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"status\":\"error\", \"message\":\"JSON is not an object\"}";
                clientSocket->write(response.toUtf8());
            }
        } else {
            QString response = "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n{\"status\":\"error\", \"message\":\"Invalid request\"}";
            clientSocket->write(response.toUtf8());
        }
    } else if (method == "GET" && path == "/") {
        // 简单的根路径响应
        QString response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nDownloader HTTP Server is running";
        clientSocket->write(response.toUtf8());
    } else {
        // 404 Not Found
        QString response = "HTTP/1.1 404 Not Found\r\n\r\n";
        clientSocket->write(response.toUtf8());
    }
    
    clientSocket->disconnectFromHost();
}
#endif