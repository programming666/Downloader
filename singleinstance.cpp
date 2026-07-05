#include "singleinstance.h"
#include "logger.h"

#include <QLocalServer>
#include <QLocalSocket>

bool SingleInstance::tryForward(const QByteArray& payload, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kServerName));
    if (!socket.waitForConnected(timeoutMs)) {
        LOGD(QString("SingleInstance::tryForward: 连接主实例失败 (%1)，转 self-start 路径")
             .arg(socket.errorString()));
        return false;
    }
    socket.write(payload);
    if (!socket.waitForBytesWritten(timeoutMs)) {
        LOGD(QString("SingleInstance::tryForward: 写 payload 失败 (%1)").arg(socket.errorString()));
        socket.disconnectFromServer();
        return false;
    }
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState) {
        socket.waitForDisconnected(timeoutMs);
    }
    LOGD(QString("SingleInstance::tryForward: 成功转发 %1 字节给主实例")
         .arg(payload.size()));
    return true;
}

SingleInstance::SingleInstance(QObject* parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    // QLocalServer 是 child，会随 this 析构；不需要手动 delete。
}

bool SingleInstance::startListening()
{
    if (m_server) {
        return true;
    }
    m_server = new QLocalServer(this);
    // 防御性 remove：万一上次进程崩溃 / 上次进程没正常关闭 socket。
    QLocalServer::removeServer(QString::fromLatin1(kServerName));
    if (!m_server->listen(QString::fromLatin1(kServerName))) {
        LOGD(QString("SingleInstance::startListening: listen 失败 - %1")
             .arg(m_server->errorString()));
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    LOGD(QString("SingleInstance::startListening: 监听 %1 已就绪").arg(QString::fromLatin1(kServerName)));
    return true;
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket* client = m_server->nextPendingConnection()) {
        // 读完整个 payload（v1 一行 < 4KB）。非阻塞 + 半秒超时。
        QByteArray buf;
        while (client->bytesAvailable() > 0 || client->waitForReadyRead(500)) {
            buf += client->readAll();
            // 单行 payload：以 '\n' 终止即可；超过 64KB 主动断。
            if (buf.contains('\n') || buf.size() > 64 * 1024) {
                break;
            }
        }
        // 兼容 EOF（对端 close）后再无 '\n'：也当作单行处理。
        if (!buf.isEmpty() && !buf.endsWith('\n')) {
            buf.append('\n');
        }
        LOGD(QString("SingleInstance: 收到转发 payload (%1 字节)").arg(buf.size()));
        if (!buf.isEmpty()) {
            emit messageReceived(buf);
        }
        client->disconnectFromServer();
        client->deleteLater();
    }
}
