#include "mainwindow.h"
#include "settingsmanager.h"
#include "historymanager.h"
#include "schedulemanager.h"
#include "downloadmanager.h"
#include "httpserver.h"
#include "singleinstance.h"
#include "protocolregistrar.h"
#include "logger.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QLocale>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QTimer>
#include <QByteArray>
#include <QDir>

namespace {

/**
 * @brief 从命令行首个非 option 参数尝试提 URL：http(s)://... 或 downloader://http(s)://...
 * 协议唤起的 argv 形如："downloader://https%3A%2F%2Fexample.com%2Ffile.zip"。
 * @return 真实 http/https URL；为空表示未识别。
 */
QString extractUrlFromArg(const QString& arg)
{
    if (arg.isEmpty()) return {};
    // 直接是 http(s)://
    if (arg.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
        || arg.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return arg;
    }
    // 协议 URL: downloader://<encoded-real-url>
    const QString prefix = QString::fromLatin1(ProtocolRegistrar::kScheme) + QStringLiteral("://");
    if (arg.startsWith(prefix, Qt::CaseInsensitive)) {
        QString real = arg.mid(prefix.size());
        // 一些浏览器把冒号等做 URL-encode（%3A）；用 QUrl::fromPercentEncoding 解码
        const QByteArray decoded = QByteArray::fromPercentEncoding(real.toUtf8());
        real = QString::fromUtf8(decoded);
        if (real.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
            || real.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
            return real;
        }
        // 也可能 downloader://https://example.com/... 没编码；保留 raw
        return arg.mid(prefix.size());
    }
    return {};
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置组织和应用程序名称，用于QSettings和QStandardPaths
    QCoreApplication::setOrganizationName("Programming666");
    QCoreApplication::setApplicationName("Downloader");

    // 命令行解析：识别 --register-protocol / --unregister-protocol / --open
    QCommandLineParser parser;
    parser.setApplicationDescription("Programming666 multi-thread downloader");
    parser.addHelpOption();
    QCommandLineOption optRegister(QStringLiteral("register-protocol"),
                                    QStringLiteral("Register downloader:// URL scheme to current exe and exit."));
    QCommandLineOption optUnregister(QStringLiteral("unregister-protocol"),
                                      QStringLiteral("Unregister downloader:// URL scheme and exit."));
    QCommandLineOption optOpen(QStringLiteral("open"),
                                QStringLiteral("Receive a download URL (use with --open <url>); can also be positional."),
                                QStringLiteral("url"));
    parser.addOption(optRegister);
    parser.addOption(optUnregister);
    parser.addOption(optOpen);
    parser.addPositionalArgument(QStringLiteral("url"),
                                 QStringLiteral("Optional downloader:// (or http/https) URL."),
                                 QStringLiteral("[url]"));
    parser.process(a);

    // 预热 SettingsManager（CLI 路径需要它持久化注册状态）
    SettingsManager::instance();

    // CLI 注册 / 取消注册：直接执行完退出，不进事件循环
    if (parser.isSet(optRegister)) {
        const bool ok = ProtocolRegistrar::registerWithCurrentExe();
        if (ok) {
            SettingsManager::instance().saveProtocolRegistered(true);
            SettingsManager::instance().saveProtocolTargetPath(ProtocolRegistrar::currentExePath());
        }
        // 释放 SettingsManager 让 QSettings sync 落盘；并显式排空事件队列
        // （QSettings::sync 需要事件循环触发 deferred sync 在某些 Windows API back-end 下）
        QCoreApplication::processEvents();
        return ok ? 0 : 1;
    }
    if (parser.isSet(optUnregister)) {
        const bool ok = ProtocolRegistrar::unregister();
        if (ok) {
            SettingsManager::instance().saveProtocolRegistered(false);
        }
        QCoreApplication::processEvents();
        return ok ? 0 : 1;
    }

    // 提取 URL：先看 --open 选项，否则看 positional 第一项
    QString pendingUrl;
    if (parser.isSet(optOpen)) {
        pendingUrl = parser.value(optOpen);
    } else {
        const QStringList pos = parser.positionalArguments();
        if (!pos.isEmpty()) {
            pendingUrl = pos.first();
        }
    }
    const QString realUrl = extractUrlFromArg(pendingUrl);
    const bool hasIncomingUrl = !realUrl.isEmpty();

    // 单实例转发：若已有实例在运行，把 URL 转发给旧实例后退出
    if (hasIncomingUrl) {
        const QString prefix = QString::fromLatin1(ProtocolRegistrar::kScheme) + QStringLiteral("://");
        const QByteArray payload = (prefix + realUrl + QLatin1Char('\n')).toUtf8();
        LOGD(QString("main: 尝试单实例转发: %1").arg(QString::fromUtf8(payload)));
        if (SingleInstance::tryForward(payload)) {
            LOGD("main: 转发成功，进程退出");
            return 0;
        }
        LOGD("main: 转发失败（旧实例不在），继续 self-start 路径");
    }

    // 预热剩下的单例
    HistoryManager::instance();
    ScheduleManager::instance();
    DownloadManager::instance();

    // 启动期翻译：由 MainWindow 的构造函数负责读取持久化语言、构造时调用
    // switchLanguage 来安装翻译器（同时也是单一 m_translator 指针的所有者，
    // 析构时统一释放）。main.cpp 这里只保留一条 LOGD 用于排查。
    LOGD(QString("System locale: %1 (MainWindow will resolve startup language)")
             .arg(QLocale::system().name()));

    // 创建并显示主窗口
    MainWindow w;
    w.show();

    // 启动 HTTP 服务器（接收浏览器插件请求）
    HttpServer* httpServer = new HttpServer(&w);
    quint16 listenPort = SettingsManager::instance().loadLocalListenPort();
    if (!httpServer->startServer(listenPort)) {
        qCritical() << "Failed to start HTTP server on port" << listenPort;
    } else {
        LOGD(QString("HTTP server listening on port %1").arg(listenPort));
    }
    QObject::connect(httpServer, &HttpServer::newDownloadRequest,
                     &w, &MainWindow::onNewDownloadRequestFromBrowser);

    // 端到端测试钩子（保留现有行为）
    const QByteArray autoPauseMs = qgetenv("DOWNLOADER_AUTO_PAUSE_MS");
    if (!autoPauseMs.isEmpty()) {
        bool ok = false;
        const int ms = autoPauseMs.toInt(&ok);
        if (ok && ms > 0) {
            QObject::connect(&DownloadManager::instance(), &DownloadManager::taskAdded,
                             &a, [ms](DownloadTask* task) {
                LOGD(QString("DOWNLOADER_AUTO_PAUSE_MS: 调度 %1ms 后 pause task %2")
                     .arg(ms).arg(task->fileName()));
                QTimer::singleShot(ms, qApp, [task]() {
                    LOGD(QString("DOWNLOADER_AUTO_PAUSE_MS: 触发 pause task %1").arg(task->fileName()));
                    DownloadManager::instance().pauseTask(task);
                    LOGD(QString("DOWNLOADER_AUTO_PAUSE_MS: pauseTask 返回 task=%1").arg(task->fileName()));
                });
            });
            qInfo() << "DOWNLOADER_AUTO_PAUSE_MS:" << ms
                    << "- pause first Downloading task after that delay";
        }
    }

    // 启动入口 URL：Self-start 路径下（没旧实例接），把 URL 喂到 MainWindow 入队
    if (hasIncomingUrl) {
        // 延后一拍，等 MainWindow 的 onUiUpdateTimer 等 ready
        const QString captured = realUrl;
        QTimer::singleShot(0, &w, [&w, captured]() {
            w.handleInitialPayload(captured);
        });
    }

    return a.exec();
}
