#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QMainWindow>

/**
 * @brief SystemTray类用于管理应用程序的系统托盘图标和相关功能。
 * 它负责创建托盘图标、上下文菜单，并处理与托盘相关的用户交互。
 */
class SystemTray : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 构造函数。
     * @param mainWindow 指向主窗口的指针，用于显示/隐藏操作。
     * @param parent 父QObject。
     */
    explicit SystemTray(QMainWindow* mainWindow, QObject *parent = nullptr);
    ~SystemTray();

    /**
     * @brief 显示系统托盘图标。
     */
    void show();

    /**
     * @brief 检查系统托盘图标是否可见。
     * @return 如果可见则返回true，否则返回false。
     */
    bool isVisible() const;

    /**
     * @brief 显示一个消息通知。
     * @param title 通知的标题。
     * @param message 通知的内容。
     * @param icon 通知的图标类型。
     * @param msecsTimeout 显示的毫秒数。
     */
    void showMessage(const QString& title, const QString& message,
                     QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information,
                     int msecsTimeout = 10000);

private slots:
    /**
     * @brief 处理托盘图标被激活（例如，点击）的槽函数。
     * @param reason 激活的原因（例如，单击、双击）。
     */
    void onIconActivated(QSystemTrayIcon::ActivationReason reason);

    /**
     * @brief 显示主窗口的槽函数。
     */
    void onShowMainWindow();

    /**
     * @brief 退出应用程序的槽函数。
     */
    void onQuitApplication();

private:
    /**
     * @brief 创建托盘图标的上下文菜单。
     */
    void createActions();

    QMainWindow* m_mainWindow;      ///< 指向主窗口的指针。
    QSystemTrayIcon* m_trayIcon;    ///< 系统托盘图标实例。
    QMenu* m_trayMenu;              ///< 托盘图标的上下文菜单。

    QAction* m_showAction;          ///< “显示主窗口”动作。
    QAction* m_quitAction;          ///< “退出”动作。
};

#endif // SYSTEMTRAY_H
