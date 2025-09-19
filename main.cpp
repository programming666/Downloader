#include "mainwindow.h"
#include "settingsmanager.h"
#include "historymanager.h"
#include "localserver.h" // 包含LocalServer头文件
#include "logger.h"

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
/**
 * @brief 应用程序主入口函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 应用程序退出码
 * 
 * 主函数负责：
 * 1. 创建QApplication实例
 * 2. 设置应用程序元数据（组织名称、应用名称）
 * 3. 初始化单例管理器（设置、历史记录）
 * 4. 加载国际化翻译文件
 * 5. 创建并显示主窗口
 * 6. 启动本地服务器接收浏览器插件请求
 * 7. 进入Qt事件循环
 */
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置组织和应用程序名称，用于QSettings和QStandardPaths
    // 这些信息用于确定配置文件和数据的存储位置
    QCoreApplication::setOrganizationName("Programming666");
    QCoreApplication::setApplicationName("Downloader");

    // 初始化SettingsManager和HistoryManager单例
    // 确保在程序早期初始化，避免后续使用时的竞态条件
    SettingsManager::instance();
    HistoryManager::instance();

    // 加载国际化翻译文件
    // 根据系统语言环境自动选择中文或英文界面
    QTranslator translator;
    QString locale = QLocale::system().name(); // 获取系统语言环境
    LOGD(QString("System locale: %1").arg(locale));

    // 尝试加载中文翻译（系统语言为中文时）
    if (locale.startsWith("zh")) {
        if (translator.load(":/translations/zh_CN.qm")) {
            a.installTranslator(&translator);
            LOGD("Loaded Chinese translation from resources.");
        } else {
            // 资源文件加载失败时，尝试从应用程序目录加载
            QString appDir = QCoreApplication::applicationDirPath();
            if (translator.load(appDir + "/translations/zh_CN.qm")) {
                a.installTranslator(&translator);
                LOGD("Loaded Chinese translation from application directory.");
            } else if (translator.load("translations/zh_CN.qm")) {
                a.installTranslator(&translator);
                LOGD("Loaded Chinese translation from current directory.");
            } else {
                LOGD("Failed to load Chinese translation file.");
            }
        }
    } else {
        // 非中文系统，加载英文翻译
        if (translator.load(":/translations/en_US.qm")) {
            a.installTranslator(&translator);
            LOGD("Loaded English translation from resources.");
        } else {
            // 资源文件加载失败时，尝试从应用程序目录加载
            QString appDir = QCoreApplication::applicationDirPath();
            if (translator.load(appDir + "/translations/en_US.qm")) {
                a.installTranslator(&translator);
                LOGD("Loaded English translation from application directory.");
            } else if (translator.load("translations/en_US.qm")) {
                a.installTranslator(&translator);
                LOGD("Loaded English translation from current directory.");
            } else {
                LOGD("Failed to load English translation.");
            }
        }
    }

    // 创建并显示主窗口
    MainWindow w;
    w.show();

    // 启动本地服务器，接收浏览器插件的下载请求
    LocalServer localServer;
    quint16 listenPort = SettingsManager::instance().loadLocalListenPort();
    if (!localServer.startServer(listenPort)) {
        qCritical() << "Failed to start local server on port" << listenPort;
    } else {
        LOGD(QString("Local server listening on port %1").arg(listenPort));
    }
    
    // 连接本地服务器信号到主窗口槽函数
    // 当浏览器插件发送下载请求时，主窗口会弹出新建任务对话框
    QObject::connect(&localServer, &LocalServer::newDownloadRequest,
                     &w, &MainWindow::onNewDownloadRequestFromBrowser);

    // 进入Qt事件循环，等待用户交互和系统事件
    // 应用程序将保持运行状态，直到用户选择退出
    return a.exec();
}
