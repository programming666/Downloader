#ifndef LOGGER_H
#define LOGGER_H

/**
 * @file logger.h
 * @brief 简单的日志记录工具
 *
 * 提供基本的日志记录功能，将日志信息写入指定文件
 * 包含时间戳、文件名和行号信息
 */

#include <QFile>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QSettings>
#include <QDir>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#include <mutex>

/**
 * @brief 解析日志路径。优先尝试 logs_config.txt；否则使用
 *        QStandardPaths::AppDataLocation + "/downloader.log"；最后回退到 ./downloader.log。
 * @return 日志文件路径
 */
static QString getLogPath() {
    QString configPath = QDir::current().absoluteFilePath("logs_config.txt");
    if (QFile::exists(configPath)) {
        QSettings settings(configPath, QSettings::IniFormat);
        return settings.value("log_path").toString();
    }
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        QDir d(appData);
        if (!d.exists()) {
            d.mkpath(".");
        }
        return d.filePath("downloader.log");
    }
    return QDir::current().filePath("downloader.log");
}

/**
 * @brief 获取一个进程内共享的日志文件句柄。函数局部static + std::call_once 保证
 *        只被打开一次，避免每次 log 调用 open/close 的开销以及静态初始化竞态。
 * @param outFile [out] 接收已经打开的 QFile* 指针的引用。
 * @return 成功打开返回 true；失败返回 false（调用方应静默放弃）。
 */
static bool getLogFile(QFile*& outFile)
{
    static QFile* s_file = nullptr;
    static std::once_flag s_once;
    static bool s_ok = false;

    std::call_once(s_once, []() {
        const QString path = getLogPath();
        QFile* f = new QFile(path);
        if (f->open(QIODevice::WriteOnly | QIODevice::Append)) {
            s_file = f;
            s_ok = true;
        } else {
            delete f;
            s_ok = false;
        }
    });

    outFile = s_file;
    return s_ok;
}

/**
 * @brief 进程退出时关闭并释放日志文件句柄。
 * 通过 QCoreApplication::aboutToQuit 信号连接触发。
 */
static void shutdownLogger()
{
    static QFile* s_file = nullptr;
    static std::once_flag s_once;
    std::call_once(s_once, []() {
        QFile* f = nullptr;
        getLogFile(f);
        s_file = f;
    });
    if (s_file) {
        s_file->flush();
        s_file->close();
        delete s_file;
        s_file = nullptr;
    }
}

/**
 * @brief 进程内全局互斥锁，保证多线程写入文件不会交错。
 */
static QMutex& logMutex()
{
    static QMutex m;
    return m;
}

/**
 * @brief 将日志消息写入文件（线程安全）。
 * @param message 要记录的日志消息
 *
 * 日志格式：[时间] 消息内容
 * 日志文件路径通过 getLogPath() 解析，默认 AppDataLocation 下的 downloader.log；
 * 如果打开失败则静默失败。
 */
static void logToFile(const QString& message) {
    QFile* f = nullptr;
    if (!getLogFile(f) || !f) {
        return;
    }
    // 调用方（LOGD 宏）已持 logMutex()，此处不再加锁。
    // 把整条日志（时间戳 + 消息 + "\n"）一次性构造成 QByteArray 后单次
    // QFile::write()，绕过 QTextStream 的内部 QString 缓冲。日志行通常
    // < 200 字节，远小于 Win32 WriteFile/POSIX write(2) 的原子写入上限，
    // 配合锁可保证多线程写入不会在字节层面交错。
    const QByteArray line =
        QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ").toUtf8()
        + message.toUtf8()
        + '\n';
    f->write(line);
    f->flush();
}

/**
 * @brief 日志记录宏
 * @param x 要记录的日志消息
 *
 * 使用示例：
 * @code
 * LOGD("This is a debug message");
 * @endcode
 *
 * 输出格式示例：
 * [2023-01-01 12:00:00.000] [mainwindow.cpp:123] This is a debug message
 *
 * 宏展开时持有进程内全局互斥锁，写入完成后释放。
 */
#define LOGD(x) do { \
        QString _msg = QString("[%1:%2] %3").arg(__FILE__).arg(__LINE__).arg((x)); \
        QMutexLocker _log_locker(&logMutex()); \
        logToFile(_msg); \
    } while (0)

#endif // LOGGER_H
