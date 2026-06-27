#include "mainwindow.h"
#include "settingsmanager.h"
#include "historymanager.h"
#include "httpserver.h" // 包含HttpServer头文件
#include "logger.h"

#include <QApplication>
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
 * 4. 创建并显示主窗口（翻译器由 MainWindow 接管）
 * 5. 启动本地服务器接收浏览器插件请求
 * 6. 进入Qt事件循环
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

    // 翻译器由 MainWindow 接管：MainWindow 构造时会根据系统语言加载并 install，
    // 后续用户切换语言也由 MainWindow 统一管理（QTranslator 生命周期由 m_translator 持有）。
    LOGD(QString("System locale: %1").arg(QLocale::system().name()));

    // 创建并显示主窗口
    MainWindow w;
    w.show();

    // 启动HTTP服务器，接收浏览器插件的下载请求
    // 使用堆对象并以主窗口为父对象，确保释放顺序：HttpServer 在 MainWindow 之前析构
    HttpServer* httpServer = new HttpServer(&w);
    quint16 listenPort = SettingsManager::instance().loadLocalListenPort();
    if (!httpServer->startServer(listenPort)) {
        qCritical() << "Failed to start HTTP server on port" << listenPort;
    } else {
        LOGD(QString("HTTP server listening on port %1").arg(listenPort));
    }

    // 连接HTTP服务器信号到主窗口槽函数
    // 当浏览器插件发送下载请求时，主窗口会弹出新建任务对话框
    QObject::connect(httpServer, &HttpServer::newDownloadRequest,
                     &w, &MainWindow::onNewDownloadRequestFromBrowser);

    // 进入Qt事件循环，等待用户交互和系统事件
    // 应用程序将保持运行状态，直到用户选择退出
    return a.exec();
}
