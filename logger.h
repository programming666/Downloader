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
#include <QTextStream>
#include <QDateTime>
#include <QString>
#include <QSettings>
#include <QDir>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QMutex>

/**
 * @brief 从配置文件读取日志路径
 *
 * 配置文件位置优先级：
 *   1. 应用目录下的 logs_config.txt（与可执行文件放一起，便于部署）
 *   2. 用户配置目录
 * 日志路径字段 [log_path]，未配置时使用
 *   QStandardPaths::AppLocalDataLocation/downloader.log（跨平台默认）。
 *
 * 结果会缓存在静态变量里，避免每次 LOGD 调用都读配置 / 解析标准路径。
 */
static QString resolveLogPath() {
    static const QString cachedPath = []() -> QString {
        // Qt 6 中可用的标准位置枚举：AppLocalDataLocation（推荐用作 app 私有数据目录），
        // AppDataLocation 是它的别名（自 Qt 5.4 起）。
        // AppLogLocation 在 Qt 6.5 之前不存在，不能用。
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
        if (defaultPath.isEmpty()) {
            defaultPath = QDir::homePath();
        }
        defaultPath = QDir(defaultPath).absoluteFilePath("downloader.log");

        // 优先：可执行文件同目录的 logs_config.txt
        QString exeDirConfig = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("logs_config.txt");
        if (QFile::exists(exeDirConfig)) {
            QSettings settings(exeDirConfig, QSettings::IniFormat);
            QString p = settings.value("log_path", defaultPath).toString();
            if (!p.isEmpty()) return p;
        }
        // 备选：CWD 下的 logs_config.txt（兼容从命令行启动的场景）
        QString cwdConfig = QDir::current().absoluteFilePath("logs_config.txt");
        if (QFile::exists(cwdConfig)) {
            QSettings settings(cwdConfig, QSettings::IniFormat);
            QString p = settings.value("log_path", defaultPath).toString();
            if (!p.isEmpty()) return p;
        }
        return defaultPath;
    }();
    return cachedPath;
}

/**
 * @brief 将日志消息写入文件
 * @param message 要记录的日志消息
 *
 * 日志格式：[时间] 消息内容
 * 整段写文件过程加互斥锁，保证多线程并发调用安全。
 * 如果文件打开失败，则静默失败。
 */
static void logToFile(const QString& message) {
    static QMutex logMutex;
    QMutexLocker locker(&logMutex);

    QString logPath = resolveLogPath();
    // 确保父目录存在
    QFileInfo fi(logPath);
    QDir().mkpath(fi.absolutePath());

    QFile f(logPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8); // 设置UTF-8编码
    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") << message << "\n";
    // QTextStream 析构会 flush；显式关闭保险一些
    f.close();
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
 */
#define LOGD(x) logToFile(QString("[%1:%2] %3").arg(__FILE__).arg(__LINE__).arg(x))

#endif // LOGGER_H