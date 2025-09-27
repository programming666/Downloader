#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableWidget>
#include <QProgressBar>
#include <QProgressDialog>
#include <QLabel>
#include <QCloseEvent>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QFutureWatcher>
#include <QSystemTrayIcon> // For system notifications
#include <QHeaderView>
#include <QTimer>
#include <QResizeEvent>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include "downloadmanager.h"
#include "systemtray.h"
#include "settingsmanager.h"
#include "historymanager.h"
#include "schedulemanager.h"
#include "scheduledialog.h"
#include "historydialog.h"
#include <QSet>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

public slots:
    /**
     * @brief 处理来自浏览器插件的新下载请求。
     * @param url 文件的URL。
     * @param savePath 建议的保存路径。
     */
    void onNewDownloadRequestFromBrowser(const QString& url, const QString& savePath);

private slots:
    /**
     * @brief 处理“新建任务”按钮点击事件。
     */
    void on_actionNewTask_triggered();

    /**
     * @brief 处理“全部开始”按钮点击事件。
     */
    void on_actionStartAll_triggered();

    /**
     * @brief 处理“全部暂停”按钮点击事件。
     */
    void on_actionPauseAll_triggered();

    /**
     * @brief 处理“取消选中”按钮点击事件。
     */
    void on_actionCancelSelected_triggered();

    /**
     * @brief 处理“删除选中”按钮点击事件。
     */
    void on_actionDeleteSelected_triggered();

    /**
     * @brief 处理“暂停选中”按钮点击事件。
     */
    void on_actionPauseSelected_triggered();

    /**
     * @brief 处理“继续选中”按钮点击事件。
     */
    void on_actionResumeSelected_triggered();

    /**
     * @brief 处理“设置”按钮点击事件。
     */
    void on_actionSettings_triggered();

    /**
     * @brief 处理“关于”按钮点击事件。
     */
    void on_actionAbout_triggered();

    /**
     * @brief 处理DownloadManager发出的taskAdded信号。
     * @param task 新添加的下载任务。
     */
    void onTaskAdded(DownloadTask* task);

    /**
     * @brief 处理DownloadTask发出的statusChanged信号。
     * @param status 新的任务状态。
     */
    void onTaskStatusChanged(DownloadTaskStatus status);

    /**
     * @brief 处理DownloadTask发出的progressUpdated信号。
     * @param bytesReceived 已下载的字节数。
     * @param totalBytes 文件总字节数。
     * @param speed 当前下载速度。
     */
    void onTaskProgressUpdated(qint64 bytesReceived, qint64 totalBytes, qint64 speed);

    /**
     * @brief 处理DownloadTask发出的finished信号。
     */
    void onTaskFinished();

    /**
     * @brief 处理DownloadTask发出的error信号。
     * @param errorString 错误信息。
     */
    void onTaskError(const QString& errorString);

    /**
     * @brief 处理SettingsManager发出的themeChanged信号。
     * @param themeName 新的主题名称。
     */
    void onThemeChanged(const QString& themeName);

    /**
     * @brief 处理语言切换为中文。
     */
    void on_actionChinese_triggered();

    /**
     * @brief 处理语言切换为英文。
     */
    void on_actionEnglish_triggered();

    /**
     * @brief 切换应用程序语言。
     * @param language 语言代码 ("zh_CN" 或 "en_US").
     */
    void switchLanguage(const QString& language);

    /**
     * @brief 更新语言菜单状态。
     */
    void updateLanguageMenu();

    /**
     * @brief 处理定时下载任务触发。
     * @param task 触发的定时任务。
     */
    void onScheduledTaskTriggered(const ScheduledTask& task);

    /**
     * @brief 处理定时任务列表改变。
     */
    void onScheduledTasksChanged();

    /**
     * @brief 处理定时下载按钮点击事件。
     */
    void on_actionScheduleTask_triggered();

    /**
     * @brief 处理查看历史记录按钮点击事件。
     */
    void on_actionViewHistory_triggered();

private slots:
    /**
     * @brief 定时更新UI，用于节流。
     */
    void onUiUpdateTimerTimeout();

protected:
    /**
     * @brief 重写closeEvent，实现最小化到托盘。
     * @param event 关闭事件。
     */
    void closeEvent(QCloseEvent *event) override;
    
    /**
     * @brief 重写resizeEvent，响应窗口大小变化以调整表格布局。
     * @param event 窗口大小变化事件。
     */
    void resizeEvent(QResizeEvent *event) override;

private:
    Ui::MainWindow *ui;                 ///< 主窗口UI组件
    DownloadManager& m_downloadManager; ///< 下载管理器实例，负责所有下载任务的调度和管理
    SettingsManager& m_settingsManager; ///< 设置管理器实例，处理用户偏好设置和持久化
    HistoryManager& m_historyManager;   ///< 历史管理器实例，记录和管理下载历史
    SystemTray* m_systemTray;           ///< 系统托盘实例，提供后台运行和通知功能
    QTimer* m_uiUpdateTimer;            ///< UI更新定时器，用于节流频繁的UI更新
    QSet<DownloadTask*> m_tasksToUpdate;///< 待更新UI的任务集合，用于批量更新任务状态
    QPointer<QProgressDialog> m_pauseProgress; ///< 暂停操作进度对话框，显示批量暂停进度

    /**
     * @brief 初始化UI界面。
     */
    void initUI();

    /**
     * @brief 加载QSS样式文件。
     * @param themeName 主题名称。
     */
    void loadStyleSheet(const QString& themeName);

    /**
     * @brief 将字节数转换为可读的字符串（例如：KB, MB, GB）。
     * @param bytes 字节数。
     * @return 可读的字符串。
     */
    QString formatBytes(qint64 bytes) const;

    /**
     * @brief 将下载速度转换为可读的字符串。
     * @param bytesPerSecond 每秒字节数。
     * @return 可读的速度字符串。
     */
    QString formatSpeed(qint64 bytesPerSecond) const;

    /**
     * @brief 在任务列表中添加一行。
     * @param task 要添加的DownloadTask指针。
     */
    void addTaskToTable(DownloadTask* task);

    /**
     * @brief 更新任务列表中的一行。
     * @param task 要更新的DownloadTask指针。
     */
    void updateTaskInTable(DownloadTask* task);

    /**
     * @brief 从任务列表中移除一行。
     * @param task 要移除的DownloadTask指针。
     */
    void removeTaskFromTable(DownloadTask* task);

    /**
     * @brief 显示系统通知。
     * @param title 通知标题。
     * @param message 通知内容。
     * @param icon 通知图标类型。
     */
    void showSystemNotification(const QString& title, const QString& message, QSystemTrayIcon::MessageIcon icon);

    /**
     * @brief 设置表格基本属性。
     * 初始化表格的通用属性如换行、省略模式等。
     */
    void setupTableBasicProperties();

    /**
     * @brief 配置表格列宽。
     * 设置各列的最小/最大宽度和调整策略。
     */
    void configureColumnWidths();

    /**
     * @brief 设置表头行为。
     * 配置表头的交互行为和大小调整限制。
     */
    void setupHeaderBehavior();

    /**
     * @brief 设置响应式表格列宽配置。
     * 根据内容自动调整列宽，确保完整显示信息并最大化空间利用率。
     */
    void setupResponsiveTableColumns();
};
#endif // MAINWINDOW_H
