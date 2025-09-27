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

/**
 * @brief 从配置文件读取日志路径
 * @return 日志文件路径
 */
static QString getLogPath() {
    QString configPath = QDir::current().absoluteFilePath("logs_config.txt");
    if (QFile::exists(configPath)) {
        QSettings settings(configPath, QSettings::IniFormat);
        return settings.value("log_path", "D:/LOG/downloader.log").toString();
    }
    return "D:/LOG/downloader.log";
}

/**
 * @brief 将日志消息写入文件
 * @param message 要记录的日志消息
 * 
 * 日志格式：[时间] 消息内容
 * 日志文件路径从配置文件读取，默认为"D:/LOG/downloader.log"
 * 如果文件打开失败，则静默失败
 */
static void logToFile(const QString& message) {
    QString logPath = getLogPath();
    QFile f(logPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8); // 设置UTF-8编码
    ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") << message << "\n";
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
