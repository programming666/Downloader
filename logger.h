#ifndef LOGGER_H
#define LOGGER_H

#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QString>

static void logToFile(const QString& message) {
    // QFile f("D:/LOG/downloader.log");
    // if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    // QTextStream ts(&f);
    // ts << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz ") << message << "\n";
    // f.close();
}

#define LOGD(x) logToFile(QString("[%1:%2] %3").arg(__FILE__).arg(__LINE__).arg(x))

#endif // LOGGER_H
