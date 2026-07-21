#include "systemtray.h"
#include "logger.h"
#include "settingsmanager.h"
#include <QPointer>
#include <QTimer>

/**
 * @brief 系统托盘构造函数
 * @param mainWindow 主窗口指针，用于显示/隐藏主窗口
 * @param parent 父对象指针
 * 
 * 创建系统托盘图标，支持多种图标加载方式：
 * 1. 优先从Qt资源文件加载
 * 2. 从应用程序目录加载
 * 3. 从当前工作目录加载
 * 确保在不同部署环境下都能正常显示托盘图标
 */
/**
 * @brief 系统托盘构造函数
 * @param mainWindow 主窗口指针，用于显示/隐藏主窗口
 * @param parent 父对象指针
 * 
 * 创建系统托盘图标，支持多种图标加载方式：
 * 1. 优先从Qt资源文件加载
 * 2. 从应用程序目录加载
 * 3. 从当前工作目录加载
 * 确保在不同部署环境下都能正常显示托盘图标
 */
SystemTray::SystemTray(QMainWindow* mainWindow, QObject *parent)
    : QObject(parent), m_mainWindow(mainWindow)
{
    // 创建托盘图标
    m_trayIcon = new QSystemTrayIcon(this);
    
    // 尝试从资源文件加载图标
    QIcon icon(":/icon.ico");
    if (icon.isNull()) {
        // 如果资源文件加载失败，尝试从应用程序目录加载
        QString appDir = QCoreApplication::applicationDirPath();
        QIcon appIcon(appDir + "/icon.ico");
        if (!appIcon.isNull()) {
            icon = appIcon;
            qDebug() << "Loaded tray icon from application directory";
        } else {
            // 如果都失败了，尝试从当前工作目录加载
            QIcon currentDirIcon("icon.ico");
            if (!currentDirIcon.isNull()) {
                icon = currentDirIcon;
                qDebug() << "Loaded tray icon from current directory";
            } else {
                qWarning() << "Failed to load tray icon from all locations";
            }
        }
    } else {
        qDebug() << "Loaded tray icon from resources";
    }
    
    m_trayIcon->setIcon(icon);
    m_trayIcon->setToolTip(tr("Downloader"));

    // 创建菜单（parent 设为 mainWindow，避免 leak 并跟随主窗口生命周期一起释放）
    // QMenu 的 parent 必须是 QWidget，因此用 m_mainWindow。
    m_trayMenu = new QMenu(m_mainWindow);
    createActions();

    // 连接信号和槽
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &SystemTray::onIconActivated);

    // 设置托盘图标菜单
    m_trayIcon->setContextMenu(m_trayMenu);

    // 语言切换广播：实时刷新"显示主界面"/"退出"的 menu 文案与 tooltip，
    // 避免在英文界面下仍显示"显示主界面"。
    connect(&SettingsManager::instance(), &SettingsManager::languageChanged,
            this, [this](const QString&) {
        if (m_showAction) m_showAction->setText(tr("显示主界面"));
        if (m_quitAction) m_quitAction->setText(tr("退出"));
        m_trayIcon->setToolTip(tr("Downloader"));
    });
}

SystemTray::~SystemTray()
{
    // m_trayIcon、m_showAction、m_quitAction 都是 SystemTray 的子对象，
    // 会在 SystemTray 析构时自动删除。
    // m_trayMenu 现在 parent 是 m_mainWindow，会跟随主窗口一起释放，无需手动 delete。
    m_trayMenu = nullptr;
}

/**
 * @brief 创建托盘图标右键菜单
 * 
 * 创建包含以下选项的上下文菜单：
 * - 显示主窗口：恢复并显示主窗口
 * - 退出：退出整个应用程序
 * 
 * 菜单项通过信号槽连接到对应的处理函数
 */
/**
 * @brief 创建系统托盘菜单动作
 * 
 * 创建右键菜单的动作项：
 * - 显示主界面：显示/隐藏主窗口
 * - 退出：退出应用程序
 * 
 * 连接动作的触发信号到对应的槽函数
 * 提供用户与托盘图标的交互功能
 */
void SystemTray::createActions()
{
    // 创建菜单项
    m_showAction = new QAction(tr("显示主界面"), this);
    m_quitAction = new QAction(tr("退出"), this);

    // 连接信号槽
    connect(m_showAction, &QAction::triggered, this, &SystemTray::onShowMainWindow);
    connect(m_quitAction, &QAction::triggered, this, &SystemTray::onQuitApplication);

    // 组装菜单
    m_trayMenu->addAction(m_showAction);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(m_quitAction);
}

void SystemTray::show()
{
    m_trayIcon->show();
}

bool SystemTray::isVisible() const
{
    return m_trayIcon->isVisible();
}

void SystemTray::showMessage(const QString& title, const QString& message,
                             QSystemTrayIcon::MessageIcon icon, int msecsTimeout)
{
    m_trayIcon->showMessage(title, message, icon, msecsTimeout);
}

/**
 * @brief 处理托盘图标激活事件
 * @param reason 激活原因
 * 
 * 响应用户的托盘图标操作：
 * - 单击或双击：显示主窗口
 * - 其他操作：忽略
 * 
 * 通过switch语句处理不同的激活原因
 */
void SystemTray::onIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    switch (reason) {
    case QSystemTrayIcon::Trigger:      // 单击
    case QSystemTrayIcon::DoubleClick:  // 双击
        onShowMainWindow();
        break;
    default:
        break;
    }
}

/**
 * @brief 显示主窗口
 * 
 * 恢复并激活主窗口，确保其在最前端显示
 * 调用顺序：showNormal() -> raise() -> activateWindow()
 * 这个顺序可以确保窗口正确显示并获得焦点
 */
void SystemTray::onShowMainWindow()
{
    if (!m_mainWindow) {
        return;
    }
    // 在某些平台（尤其 Windows + 已隐藏窗口），连续调用 showNormal()/raise()/activateWindow()
    // 会与系统事件循环产生竞争导致窗口不显示或闪烁。改为延迟到事件循环下一次迭代执行，
    // 让窗口的当前状态先稳定下来再显示。
    QTimer::singleShot(0, this, [mainWindow = QPointer<QMainWindow>(m_mainWindow)]() {
        if (!mainWindow) return;
        if (mainWindow->isMinimized()) {
            mainWindow->showNormal();
        } else {
            mainWindow->show();
        }
        mainWindow->raise();
        mainWindow->activateWindow();
    });
}

/**
 * @brief 退出应用程序
 *
 * 响应用户退出操作，终止整个应用程序。
 * 通过调用主窗口的 requestQuit() 设置退出标志，
 * 再触发 QApplication::quit() 退出事件循环。
 */
void SystemTray::onQuitApplication()
{
    if (m_mainWindow) {
        QMetaObject::invokeMethod(m_mainWindow, "requestQuit");
    } else {
        QApplication::quit();
    }
}
