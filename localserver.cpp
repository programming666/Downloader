#include "localserver.h"
#include <QDebug>

/**
 * @brief 本地服务器构造函数
 * @param parent 父对象指针
 * 
 * 创建TCP服务器实例，用于接收浏览器插件的下载请求
 * 服务器监听本地端口，接收JSON格式的下载任务请求
 */
/**
 * @brief 本地服务器构造函数
 * @param parent 父对象指针
 * 
 * 初始化本地服务器，用于接收浏览器插件的下载请求
 * 创建QLocalServer实例并连接新连接信号
 * 支持浏览器插件与下载器的进程间通信
 */
LocalServer::LocalServer(QObject *parent)
    : QObject(parent)
    , m_server(nullptr)
{
    LOGD("开始初始化LocalServer");
    
    LOGD("开始创建QLocalServer...");
    m_server = new QLocalServer(this);
    LOGD("QLocalServer创建完成");
    
    LOGD("开始连接信号...");
    connect(m_server, &QLocalServer::newConnection, this, &LocalServer::onNewConnection);
    LOGD("信号连接完成");
    
    LOGD("LocalServer初始化完成");
}

/**
 * @brief 本地服务器析构函数
 * 
 * 确保在对象销毁时停止服务器，释放资源
 * 调用stopServer()方法关闭监听端口
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
 * 如果服务器已经在运行，则直接返回成功
 */
/**
 * @brief 启动本地服务器监听
 * @param port 监听端口（QLocalServer使用名称而非端口）
 * @return true 启动成功，false 启动失败
 * 
 * 启动本地服务器监听指定名称，用于接收浏览器插件的连接
 * 如果服务器已在监听，先停止当前监听再重新启动
 * 启动失败时会发射错误信号
 */
bool LocalServer::startServer(quint16 port)
{
    LOGD(QString("开始启动本地服务器 - 端口:%1").arg(port));
    
    if (m_server->isListening()) {
        LOGD("服务器已在监听状态，停止当前监听");
        m_server->close();
    }
    
    LOGD("开始监听...");
    bool result = m_server->listen("downloader");
    LOGD(QString("监听结果:%1").arg(result ? "成功" : "失败"));
    
    if (!result) {
        LOGD(QString("监听失败 - 错误:%1").arg(m_server->errorString()));
        emit error(QString("无法启动服务器: %1").arg(m_server->errorString()));
        return false;
    }
    
    LOGD(QString("本地服务器启动成功 - 服务器名称:downloader"));
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
 * 为每个客户端连接创建独立的信号槽连接，处理数据读取和断开事件
 */
void LocalServer::onNewConnection()
{
    QTcpSocket *clientSocket = m_tcpServer->nextPendingConnection();
    if (clientSocket) {
        connect(clientSocket, &QTcpSocket::readyRead, this, &LocalServer::onReadyRead);
        connect(clientSocket, &QTcpSocket::disconnected, this, &LocalServer::onClientDisconnected);
        qDebug() << "New client connected:" << clientSocket->peerAddress().toString();
    }
}

/**
 * @brief 处理客户端发送的数据
 * 
 * 当客户端有数据可读时触发，读取并解析JSON格式的下载请求
 * 支持以下JSON格式：
 * {
 *   "url": "下载链接",
 *   "savePath": "保存路径（可选）"
 * }
 * 
 * 解析成功后会发射newDownloadRequest信号，并回复客户端处理结果
 */
void LocalServer::onReadyRead()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (!clientSocket) {
        return;
    }

    QByteArray data = clientSocket->readAll();
    qDebug() << "Received data from client:" << data;

    // 解析JSON数据
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse JSON from client:" << parseError.errorString();
        clientSocket->write("{\"status\":\"error\", \"message\":\"Invalid JSON\"}\n");
        clientSocket->disconnectFromHost();
        return;
    }
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("url") && obj["url"].isString()) {
            QString url = obj["url"].toString();
            QString savePath = obj.value("savePath").toString(); // savePath是可选的

            qDebug() << "Received new download request. URL:" << url << "Save Path:" << savePath;
            emit newDownloadRequest(url, savePath);

            // 回复客户端表示成功
            clientSocket->write("{\"status\":\"success\", \"message\":\"Download request received\"}\n");
        } else {
            clientSocket->write("{\"status\":\"error\", \"message\":\"Missing or invalid 'url' field\"}\n");
        }
    } else {
        clientSocket->write("{\"status\":\"error\", \"message\":\"JSON is not an object\"}\n");
    }

    clientSocket->disconnectFromHost();
}

/**
 * @brief 处理客户端断开连接
 * 
 * 当客户端断开连接时触发，清理客户端套接字对象
 * 使用deleteLater()确保在事件循环中安全删除对象
 */
void LocalServer::onClientDisconnected()
{
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket*>(sender());
    if (clientSocket) {
        qDebug() << "Client disconnected:" << clientSocket->peerAddress().toString();
        clientSocket->deleteLater();
    }
}
