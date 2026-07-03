#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "logger.h"
#include "newtaskdialog.h" // 新建任务对话框
#include "settingsdialog.h" // 设置对话框
#include "historymanager.h" // 历史管理器
#include <QFileDialog>
#include <QMessageBox>
#include <QDebug>
#include <QApplication>
#include <QStyleFactory> // For QSS loading
#include <QTranslator>
#include <QLocale>
#include <QTimer> // 用于延迟启动任务
#include <QProgressDialog>
#include <utility> // 用于std::as_const
#include <memory>  // 用于std::unique_ptr
#include <QFileInfo>
#include <QDir>

/**
 * @brief 主窗口构造函数
 * @param parent 父窗口指针
 * 
 * 初始化主窗口界面和各个功能模块：
 * 1. 设置UI界面布局
 * 2. 初始化下载管理器、历史管理器、设置管理器
 * 3. 创建系统托盘图标
 * 4. 连接各个模块的信号槽
 * 5. 加载用户设置和历史记录
 * 6. 配置系统托盘功能
 */
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_downloadManager(DownloadManager::instance())
    , m_settingsManager(SettingsManager::instance())
    , m_historyManager(HistoryManager::instance()),
    m_uiUpdateTimer(new QTimer(this))
    , m_translator(nullptr)
{
    ui->setupUi(this);
    initUI();

    // 初始化系统托盘
    m_systemTray = new SystemTray(this, this);
    m_systemTray->show();

    // Qt的自动连接机制会自动连接符合命名规则的槽函数
    // 不需要手动连接，否则会导致信号被连接两次
    // connect(ui->actionNewTask, &QAction::triggered, this, &MainWindow::on_actionNewTask_triggered);
    // 其他类似的连接也不需要手动设置

    connect(&m_downloadManager, &DownloadManager::taskAdded, this, &MainWindow::onTaskAdded);
    connect(&m_settingsManager, &SettingsManager::themeChanged, this, &MainWindow::onThemeChanged);

    // 连接定时下载管理器信号
    ScheduleManager* scheduleManager = ScheduleManager::instance();
    connect(scheduleManager, &ScheduleManager::scheduledTaskTriggered, 
            this, &MainWindow::onScheduledTaskTriggered);
    connect(scheduleManager, &ScheduleManager::scheduledTasksChanged,
            this, &MainWindow::onScheduledTasksChanged);

    // 加载上次保存的主题
    loadStyleSheet(m_settingsManager.loadTheme());

    // 根据系统区域初始化当前语言代码（用于 updateLanguageMenu 判定）
    QString sysLocale = QLocale::system().name();
    m_currentLanguage = sysLocale.startsWith("zh") ? "zh_CN" : "en_US";

    // 初始化语言菜单
    updateLanguageMenu();

    // 接管翻译器：根据 m_currentLanguage 安装翻译（避免 main 与 mainwindow 双重管理）
    switchLanguage(m_currentLanguage);

    // 设置UI更新定时器
    connect(m_uiUpdateTimer, &QTimer::timeout, this, &MainWindow::onUiUpdateTimerTimeout);
    m_uiUpdateTimer->start(200); // 每200毫秒更新一次
}

MainWindow::~MainWindow()
{
    // 清理翻译器：先从应用移除再 delete，避免悬空
    if (m_translator) {
        qApp->removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    delete ui;
    // m_systemTray的父对象是MainWindow，会自动删除
}

void MainWindow::requestQuit()
{
    // 标记为正在退出，让 closeEvent 接受关闭事件
    m_quitting = true;
    QApplication::quit();
}

void MainWindow::initUI()
{
    setWindowTitle(tr("多线程下载器"));
    setWindowIcon(QIcon(":/icon.ico"));
    
    // 如果资源文件中的图标加载失败，尝试从应用程序目录加载
    if (windowIcon().isNull()) {
        QString appDir = QCoreApplication::applicationDirPath();
        QIcon appIcon(appDir + "/icon.ico");
        if (!appIcon.isNull()) {
            setWindowIcon(appIcon);
            qDebug() << "Loaded window icon from application directory";
        } else {
            // 如果都失败了，尝试从当前工作目录加载
            QIcon currentDirIcon("icon.ico");
            if (!currentDirIcon.isNull()) {
                setWindowIcon(currentDirIcon);
                qDebug() << "Loaded window icon from current directory";
            } else {
                qWarning() << "Failed to load window icon from all locations";
            }
        }
    } else {
        qDebug() << "Loaded window icon from resources";
    }

    // 设置任务列表表头
    ui->tableWidget->setColumnCount(7);
    ui->tableWidget->setHorizontalHeaderLabels({
        tr("文件名"), tr("URL"), tr("进度"), tr("大小"), tr("速度"), tr("状态"), tr("操作")
    });
    
    // 智能响应式列宽配置
    setupResponsiveTableColumns();

    ui->tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows); // 整行选中
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers); // 禁止编辑
    ui->tableWidget->setAlternatingRowColors(true); // 交替行颜色
    ui->tableWidget->setSortingEnabled(false); // 禁用排序以保持任务顺序
    ui->tableWidget->setCornerButtonEnabled(false); // 隐藏左上角按钮

    // 状态栏
    ui->statusbar->showMessage(tr("准备就绪"));
}

void MainWindow::loadStyleSheet(const QString& themeName)
{
    QString styleSheet;
    bool loaded = false;

    // 首先尝试从资源文件加载
    QFile file(QString(":/styles/%1.qss").arg(themeName));
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        styleSheet = file.readAll();
        file.close();
        qDebug() << "Loaded theme from resources:" << themeName;
        loaded = true;
    }

    // 如果资源文件加载失败，尝试从应用程序目录加载
    if (!loaded) {
        QString appDir = QCoreApplication::applicationDirPath();
        QFile absoluteFile(appDir + "/styles/" + themeName + ".qss");
        if (absoluteFile.open(QFile::ReadOnly | QFile::Text)) {
            styleSheet = absoluteFile.readAll();
            absoluteFile.close();
            qDebug() << "Loaded theme from application directory:" << themeName;
            loaded = true;
        }
    }

    // 如果都失败了，尝试从当前工作目录加载
    if (!loaded) {
        QFile currentDirFile("styles/" + themeName + ".qss");
        if (currentDirFile.open(QFile::ReadOnly | QFile::Text)) {
            styleSheet = currentDirFile.readAll();
            currentDirFile.close();
            qDebug() << "Loaded theme from current directory:" << themeName;
            loaded = true;
        }
    }

    if (!loaded) {
        qWarning() << "Failed to load theme:" << themeName;
        return;
    }

    qApp->setStyleSheet(styleSheet);

    // 把样式表也应用到所有已存在的顶层 widget（含打开着的对话框）。
    // qApp->setStyleSheet 只会影响后续构造的 widget，已存在的不会重新计算样式，
    // 需要对每个 widget 先清空再重新设置一次才能生效。
    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for (QWidget* w : topLevels) {
        if (!w || w == this) continue;
        QString old = w->styleSheet();
        w->setStyleSheet(QString());
        w->setStyleSheet(old.isEmpty() ? styleSheet : old);
    }
}

QString MainWindow::formatBytes(qint64 bytes) const
{
    if (bytes < 1024) {
        return QString("%1 B").arg(bytes);
    } else if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 2);
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 2);
    } else {
        return QString("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2);
    }
}

QString MainWindow::formatSpeed(qint64 bytesPerSecond) const
{
    if (bytesPerSecond < 1024) {
        return QString("%1 B/s").arg(bytesPerSecond);
    } else if (bytesPerSecond < 1024 * 1024) {
        return QString("%1 KB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 2);
    } else if (bytesPerSecond < 1024 * 1024 * 1024) {
        return QString("%1 MB/s").arg(bytesPerSecond / (1024.0 * 1024), 0, 'f', 2);
    } else {
        return QString("%1 GB/s").arg(bytesPerSecond / (1024.0 * 1024 * 1024), 0, 'f', 2);
    }
}

/**
 * @brief 构造大小列文本。
 * HEAD 没拿到 Content-Length 时 totalSize=0，单独显示"未知"避免"5430 B/0 B"的违和文案。
 */
QString MainWindow::formatSizeCell(qint64 downloaded, qint64 total) const
{
    if (total <= 0) {
        return formatBytes(downloaded) + "/" + tr("未知");
    }
    return formatBytes(downloaded) + "/" + formatBytes(total);
}

void MainWindow::addTaskToTable(DownloadTask* task)
{
    LOGD(QString("开始将任务添加到表格 - 任务:%1").arg(task ? task->fileName() : "空"));
    
    if (!task) {
        LOGD("任务指针为空，无法添加到表格");
        return;
    }
    
    int row = ui->tableWidget->rowCount();
    LOGD(QString("当前表格行数:%1，将在第%2行插入新任务").arg(row).arg(row));
    ui->tableWidget->insertRow(row);
    LOGD("新行插入完成");

    // 文件名
    LOGD("设置文件名列");
    QTableWidgetItem* fileNameItem = new QTableWidgetItem(task->fileName());
    fileNameItem->setToolTip(task->fileName()); // 完整文件名提示
    ui->tableWidget->setItem(row, 0, fileNameItem);
    
    // URL
    LOGD("设置URL列");
    QTableWidgetItem* urlItem = new QTableWidgetItem(task->url());
    urlItem->setToolTip(task->url()); // 完整URL提示
    ui->tableWidget->setItem(row, 1, urlItem);

    // 进度条
    LOGD("创建进度条");
    QProgressBar* progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    int progress = task->progressPercentage();
    progressBar->setValue(progress);
    ui->tableWidget->setCellWidget(row, 2, progressBar);
    LOGD(QString("进度条设置完成，初始值:%1").arg(progress));

    // 大小
    LOGD("创建大小标签");
    QLabel* sizeLabel = new QLabel(formatSizeCell(task->downloadedSize(), task->totalSize()), this);
    sizeLabel->setAlignment(Qt::AlignCenter);
    sizeLabel->setToolTip(tr("已下载: %1\n总大小: %2").arg(formatBytes(task->downloadedSize())).arg(formatBytes(task->totalSize())));
    ui->tableWidget->setCellWidget(row, 3, sizeLabel);
    LOGD("大小标签创建完成");

    // 速度
    LOGD("创建速度标签");
    QLabel* speedLabel = new QLabel(formatSpeed(task->downloadSpeed()), this);
    speedLabel->setAlignment(Qt::AlignCenter);
    speedLabel->setToolTip(tr("当前下载速度: %1").arg(formatSpeed(task->downloadSpeed())));
    ui->tableWidget->setCellWidget(row, 4, speedLabel);
    LOGD("速度标签创建完成");

    // 状态
    LOGD("创建状态标签");
    QLabel* statusLabel = new QLabel(tr("等待中"), this);
    statusLabel->setAlignment(Qt::AlignCenter);
    statusLabel->setToolTip(tr("任务状态: %1").arg(tr("等待中")));
    ui->tableWidget->setCellWidget(row, 5, statusLabel);
    LOGD("状态标签创建完成");

    // 操作按钮
    LOGD("设置操作列");
    ui->tableWidget->setItem(row, 6, new QTableWidgetItem(tr("操作")));

    // 将任务指针存储在行数据中
    LOGD("存储任务指针到行数据中");
    ui->tableWidget->item(row, 0)->setData(Qt::UserRole, QVariant::fromValue(task));
    LOGD("任务指针已存储在行数据中");

    // 连接任务信号
    LOGD("开始连接任务信号");
    connect(task, &DownloadTask::statusChanged, this, &MainWindow::onTaskStatusChanged);
    LOGD("statusChanged信号连接完成");
    connect(task, &DownloadTask::progressUpdated, this, &MainWindow::onTaskProgressUpdated);
    LOGD("progressUpdated信号连接完成");
    connect(task, &DownloadTask::finished, this, &MainWindow::onTaskFinished);
    LOGD("finished信号连接完成");
    connect(task, &DownloadTask::error, this, &MainWindow::onTaskError);
    LOGD("error信号连接完成");
    
    LOGD("任务添加到表格完成");
}

void MainWindow::updateTaskInTable(DownloadTask* task)
{
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item && item->data(Qt::UserRole).value<DownloadTask*>() == task) {
            // 更新进度条
            QProgressBar* progressBar = qobject_cast<QProgressBar*>(ui->tableWidget->cellWidget(row, 2));
            if (progressBar) {
                progressBar->setValue(task->progressPercentage());
            }

            // 更新大小
            QLabel* sizeLabel = qobject_cast<QLabel*>(ui->tableWidget->cellWidget(row, 3));
            if (sizeLabel) {
                const QString sizeText = formatSizeCell(task->downloadedSize(), task->totalSize());
                sizeLabel->setText(sizeText);
                sizeLabel->setToolTip(tr("已下载: %1\n总大小: %2").arg(formatBytes(task->downloadedSize())).arg(formatBytes(task->totalSize())));
            }

            // 更新速度
            QLabel* speedLabel = qobject_cast<QLabel*>(ui->tableWidget->cellWidget(row, 4));
            if (speedLabel) {
                QString speedText = formatSpeed(task->downloadSpeed());
                speedLabel->setText(speedText);
                speedLabel->setToolTip(tr("当前下载速度: %1").arg(speedText));
            }

            // 更新状态
            QLabel* statusLabel = qobject_cast<QLabel*>(ui->tableWidget->cellWidget(row, 5));
            if (statusLabel) {
                QString statusText;
                switch (task->status()) {
                case DownloadTaskStatus::Pending: statusText = tr("等待中"); break;
                case DownloadTaskStatus::Downloading: statusText = tr("下载中"); break;
                case DownloadTaskStatus::Paused: statusText = tr("已暂停"); break;
                case DownloadTaskStatus::Cancelled: statusText = tr("已取消"); break;
                case DownloadTaskStatus::Completed: statusText = tr("已完成"); break;
                case DownloadTaskStatus::Failed: statusText = tr("失败"); break;
                }
                statusLabel->setText(statusText);
                statusLabel->setToolTip(tr("任务状态: %1").arg(statusText));
            }
            return;
        }
    }
}

void MainWindow::removeTaskFromTable(DownloadTask* task)
{
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item && item->data(Qt::UserRole).value<DownloadTask*>() == task) {
            ui->tableWidget->removeRow(row);
            return;
        }
    }
}

void MainWindow::showSystemNotification(const QString& title, const QString& message, QSystemTrayIcon::MessageIcon icon)
{
    // 检查是否启用静默模式
    bool silentMode = m_settingsManager.loadSilentMode();
    qDebug() << "静默模式状态:" << silentMode << "标题:" << title;
    
    if (silentMode) {
        qDebug() << "静默模式已启用，不显示通知:" << title;
        return; // 静默模式下不显示通知
    }
    
    if (m_systemTray) {
        qDebug() << "显示系统通知:" << title << "-" << message;
        m_systemTray->showMessage(title, message, icon);
    }
}

void MainWindow::onNewDownloadRequestFromBrowser(const QString& url, const QString& savePath)
{
    qDebug() << "Received download request from browser plugin. URL:" << url << "Save Path:" << savePath;

    // URL 协议校验：只接受 http/https，避免浏览器插件把 file:// 等协议丢过来
    if (url.isEmpty() || !(url.startsWith("http://") || url.startsWith("https://"))) {
        qWarning() << "Rejected browser download request with invalid URL:" << url;
        showSystemNotification(tr("新下载任务被拒绝"),
                               tr("不支持的URL协议，仅支持http/https。"),
                               QSystemTrayIcon::Warning);
        return;
    }

    QUrl urlObj(url);
    QString fileName = urlObj.fileName();
    if (fileName.isEmpty()) {
        // 没有文件名时回退到默认名，避免生成空文件名导致任务无法落地
        fileName = "download";
    }

    // 计算最终保存路径：插件传入的 savePath 形态多样，按以下规则归一化
    //  (a) 空                               -> 默认下载目录 + fileName
    //  (b) 已存在的目录 / 以分隔符结尾     -> 该目录下拼 fileName
    //  (c) 裸文件名（无目录分量）           -> 默认下载目录 + 该文件名
    //  (d) 完整路径（绝对或带目录的相对路径）-> 标准化分隔符后原样使用
    const QString defaultDir = m_settingsManager.loadDefaultDownloadPath();
    QString finalSavePath;
    if (savePath.isEmpty()) {
        finalSavePath = QDir(defaultDir).absoluteFilePath(fileName);
    } else {
        const QFileInfo info(savePath);
        const bool endsWithSep = savePath.endsWith('/') || savePath.endsWith('\\');
        if (info.isDir() || endsWithSep) {
            finalSavePath = QDir(savePath).absoluteFilePath(fileName);
        } else if (info.fileName() == savePath) {
            // 关键：QFileInfo::fileName() 等于整段输入，说明完全没有目录分量。
            // HttpServer 在 savePath 为空时把 filename 兜底发过来，落入此分支。
            finalSavePath = QDir(defaultDir).absoluteFilePath(savePath);
        } else {
            finalSavePath = QDir::cleanPath(savePath);
        }
    }
    LOGD(QString("解析 savePath:%1 -> finalSavePath:%2").arg(savePath, finalSavePath));

    DownloadTask* task = m_downloadManager.createTask(urlObj, finalSavePath, m_settingsManager.loadDefaultThreads());
    if (!task) {
        qWarning() << "Failed to create download task from browser request:" << url;
        return;
    }
    m_downloadManager.startTask(task);
    showSystemNotification(tr("新下载任务"), tr("已从浏览器插件接收到下载任务：%1").arg(fileName), QSystemTrayIcon::Information);
}

/**
 * @brief 处理新建下载任务
 * 
 * 打开新建任务对话框，获取用户输入的下载信息：
 * - URL：下载链接
 * - 保存路径：文件保存位置
 * - 线程数：下载线程数量
 * 
 * 创建下载任务并启动，添加到任务列表显示
 * 支持多线程下载和断点续传功能
 */
void MainWindow::on_actionNewTask_triggered()
{
    LOGD("进入 on_actionNewTask_triggered 方法");
    
    LOGD("创建 NewTaskDialog 对象");
    NewTaskDialog dialog(this);
    LOGD("NewTaskDialog 对象创建完成");
    
    LOGD("开始执行 dialog.exec()");
    if (dialog.exec() == QDialog::Accepted) {
        LOGD("用户确认对话框，开始获取参数");
        
        LOGD("调用 dialog.url()");
        QString url = dialog.url();
        LOGD(QString("获取到 URL: %1").arg(url));
        
        LOGD("调用 dialog.savePath()");
        QString savePath = dialog.savePath();
        LOGD(QString("获取到保存路径: %1").arg(savePath));
        
        LOGD("调用 dialog.threadCount()");
        int threads = dialog.threadCount();
        LOGD(QString("获取到线程数: %1").arg(threads));

        LOGD("开始检查 URL 和路径是否为空");
        if (url.isEmpty() || savePath.isEmpty()) {
            LOGD("URL 或路径为空，显示警告对话框");
            QMessageBox::warning(this, tr("输入错误"), tr("URL和保存路径不能为空。"));
            LOGD("警告对话框显示完成，返回");
            return;
        }

        LOGD("参数验证通过，开始创建任务");
        LOGD("更新状态栏消息");
        ui->statusbar->showMessage(tr("正在准备下载任务..."));
        LOGD("状态栏消息更新完成");

        // 构造完整的文件路径：目录路径 + 文件名
        QUrl urlObj(url);
        QString fileName = urlObj.fileName();
        if (fileName.isEmpty()) {
            // 如果URL没有文件名，使用默认名称
            fileName = "download";
        }
        QString fullFilePath = QDir(savePath).absoluteFilePath(fileName);
        LOGD(QString("构造完整文件路径: 目录=%1, 文件名=%2, 完整路径=%3").arg(savePath).arg(fileName).arg(fullFilePath));

        LOGD("调用 m_downloadManager.createTask");
        DownloadTask* task = m_downloadManager.createTask(urlObj, fullFilePath, threads);
        LOGD(QString("createTask 返回，task 指针: %1").arg(task ? "有效" : "空"));
        
        if (task) {
            LOGD("任务创建成功，更新状态栏");
            ui->statusbar->showMessage(tr("任务已创建，即将开始下载..."));
            LOGD("状态栏更新完成");

            LOGD("设置 QTimer 延迟启动");
            QPointer<DownloadTask> taskPtr(task);
            QTimer::singleShot(kTaskStartDelayMs, this, [this, taskPtr]() {
                if (!taskPtr) {
                    LOGD("任务对象已被销毁，跳过启动");
                    return;
                }
                LOGD(QString("QTimer 回调执行，启动任务: %1").arg(taskPtr->fileName()));
                m_downloadManager.startTask(taskPtr.data());
            });
            LOGD("QTimer 设置完成");
        } else {
            LOGD("任务创建失败，显示错误对话框");
            QMessageBox::critical(this, tr("任务创建失败"), tr("无法创建下载任务，请检查URL和路径是否有效。"));
            LOGD("错误对话框显示完成");
        }
    } else {
        LOGD("用户取消对话框");
    }
    LOGD("退出 on_actionNewTask_triggered 方法");
}

void MainWindow::on_actionStartAll_triggered()
{
    int startedCount = 0;
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item) {
            DownloadTask* task = item->data(Qt::UserRole).value<DownloadTask*>();
            if (task && (task->status() == DownloadTaskStatus::Pending || task->status() == DownloadTaskStatus::Failed)) {
                m_downloadManager.startTask(task);
                startedCount++;
            }
        }
    }

    if (startedCount > 0) {
        ui->statusbar->showMessage(tr("已开始 %1 个任务").arg(startedCount));
    } else {
        QMessageBox::information(this, tr("提示"), tr("没有可开始的任务。"));
    }
}

void MainWindow::on_actionPauseAll_triggered()
{
    int pausedCount = 0;
    for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item) {
            DownloadTask* task = item->data(Qt::UserRole).value<DownloadTask*>();
            if (task && task->status() == DownloadTaskStatus::Downloading) {
                task->pause();
                pausedCount++;
            }
        }
    }

    if (pausedCount > 0) {
        ui->statusbar->showMessage(tr("已暂停 %1 个任务").arg(pausedCount));
    } else {
        QMessageBox::information(this, tr("提示"), tr("没有可暂停的任务。"));
    }
}

void MainWindow::on_actionCancelSelected_triggered()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择要取消的任务。"));
        return;
    }

    // 获取选中的行
    QSet<int> selectedRows;
    for (QTableWidgetItem* item : selectedItems) {
        selectedRows.insert(item->row());
    }

    int cancelledCount = 0;
    for (int row : selectedRows) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item) {
            DownloadTask* task = item->data(Qt::UserRole).value<DownloadTask*>();
            if (task && (task->status() == DownloadTaskStatus::Downloading || task->status() == DownloadTaskStatus::Paused)) {
                task->cancel(true); // 取消并删除临时文件
                cancelledCount++;
            }
        }
    }

    if (cancelledCount > 0) {
        ui->statusbar->showMessage(tr("已取消 %1 个任务").arg(cancelledCount));
    } else {
        QMessageBox::information(this, tr("提示"), tr("没有可取消的任务。"));
    }
}

void MainWindow::on_actionDeleteSelected_triggered()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择要删除的任务。"));
        return;
    }

    // 获取选中的行
    QSet<int> selectedRows;
    for (QTableWidgetItem* item : selectedItems) {
        selectedRows.insert(item->row());
    }

    // 确认删除
    int ret = QMessageBox::question(this, tr("确认删除"), 
                                   tr("确定要删除选中的 %1 个任务吗？").arg(selectedRows.size()),
                                   QMessageBox::Yes | QMessageBox::No,
                                   QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    int deletedCount = 0;
    // 从后往前删除，避免索引变化问题
    QList<int> rowsToDelete = selectedRows.values();
    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<int>());
    
    for (int row : rowsToDelete) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item) {
            DownloadTask* task = item->data(Qt::UserRole).value<DownloadTask*>();
            if (task) {
                // 取消任务（如果还在运行）
                if (task->status() == DownloadTaskStatus::Downloading || task->status() == DownloadTaskStatus::Paused) {
                    task->cancel(true);
                }
                // 从表格中移除
                ui->tableWidget->removeRow(row);
                deletedCount++;
            }
        }
    }

    if (deletedCount > 0) {
        ui->statusbar->showMessage(tr("已删除 %1 个任务").arg(deletedCount));
    }
}

void MainWindow::on_actionPauseSelected_triggered()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择要暂停的任务。"));
        return;
    }

    // 获取选中的行
    QSet<int> selectedRows;
    for (QTableWidgetItem* item : selectedItems) {
        selectedRows.insert(item->row());
    }

    // 收集需要暂停的任务
    QList<DownloadTask*> tasksToStop;
    for (int row : selectedRows) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item) {
            DownloadTask* task = item->data(Qt::UserRole).value<DownloadTask*>();
            if (task && task->status() == DownloadTaskStatus::Downloading) {
                tasksToStop.append(task);
            }
        }
    }

    if (tasksToStop.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("没有可暂停的任务。"));
        return;
    }

    // 创建进度对话框
    m_pauseProgress = new QProgressDialog(tr("正在暂停选中的任务..."),
                                        tr("取消"),
                                        0,
                                        tasksToStop.size(),
                                        this);
    m_pauseProgress->setWindowModality(Qt::WindowModal);
    m_pauseProgress->setAttribute(Qt::WA_DeleteOnClose);
    m_pauseProgress->show();

    // 原来的 QtConcurrent::map 会让 lambda 在线程池里与表格行变更并发，
    // 引发 QTableWidgetItem 写入竞争。这里改成同步循环：UI 暂停期间无新写入，
    // 通过 QMutexLocker 短暂持有 m_tableMutex 防止重入；进度条仍能反映进度。
    int done = 0;
    bool cancelled = false;
    for (DownloadTask* task : tasksToStop) {
        if (m_pauseProgress && m_pauseProgress->wasCanceled()) {
            cancelled = true;
            break;
        }
        {
            QMutexLocker locker(&m_tableMutex);
            if (task) task->pause();
        }
        ++done;
        if (m_pauseProgress) m_pauseProgress->setValue(done);
    }
    if (m_pauseProgress) {
        m_pauseProgress->setValue(tasksToStop.size());
        if (!cancelled) m_pauseProgress->accept();
    }
    Q_UNUSED(cancelled);

    // 暂停后刷新按钮可用性（避免选中已暂停的行时 Pause 仍可点的混乱）。
    refreshSelectionActionStates();
}

void MainWindow::on_actionResumeSelected_triggered()
{
    QList<QTableWidgetItem*> selectedItems = ui->tableWidget->selectedItems();
    if (selectedItems.isEmpty()) {
        QMessageBox::information(this, tr("提示"), tr("请先选择要继续的任务。"));
        return;
    }

    // 获取选中的行
    QSet<int> selectedRows;
    for (QTableWidgetItem* item : selectedItems) {
        selectedRows.insert(item->row());
    }

    for (int row : selectedRows) {
        QTableWidgetItem* item = ui->tableWidget->item(row, 0);
        if (item) {
            DownloadTask* task = item->data(Qt::UserRole).value<DownloadTask*>();
            if (task && task->status() == DownloadTaskStatus::Paused) {
                task->resume();
            }
        }
    }
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dialog(this);
    dialog.exec();
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, tr("关于多线程下载器"),
                       tr("<h3>多线程下载器 v1.0</h3>"
                          "<p>一个基于Qt 6.10.0beta3,MinGW工具链实现的高性能多线程下载工具。</p>"
                          "<p>作者：Programming666(https://github.com/programming666)</p>"
                          "<p>版权所有 © 2025</p>"));
}

void MainWindow::onTaskAdded(DownloadTask* task)
{
    LOGD(QString("接收到taskAdded信号 - 任务:%1 URL:%2").arg(task ? task->fileName() : "空").arg(task ? task->url() : "空"));

    if (task) {
        LOGD("开始添加任务到表格");
        addTaskToTable(task);
        LOGD("任务已添加到表格");

        ui->statusbar->showMessage(tr("新任务已添加：%1").arg(task->fileName()));
        LOGD("状态栏消息已更新");

        // 系统通知
        LOGD("显示系统托盘通知");
        showSystemNotification(tr("新任务"), tr("已添加任务：%1").arg(task->fileName()), QSystemTrayIcon::Information);
        LOGD("系统托盘通知已显示");

        // 有新任务时确保 UI 更新定时器处于运行状态
        if (!m_uiUpdateTimer->isActive()) {
            m_uiIdleTicks = 0;
            m_uiUpdateTimer->start(200);
        }
    } else {
        LOGD("任务指针为空，无法添加");
    }

    LOGD("任务添加处理完成");
}

void MainWindow::onTaskStatusChanged(DownloadTaskStatus status)
{
    LOGD(QString("接收到任务状态变更信号，状态:%1").arg(static_cast<int>(status)));
    
    DownloadTask* task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        LOGD(QString("任务有效，文件名:%1，开始更新表格").arg(task->fileName()));
        updateTaskInTable(task);
        LOGD("表格更新完成");
        
        QString statusText;
        switch (status) {
        case DownloadTaskStatus::Downloading: statusText = tr("开始下载：%1").arg(task->fileName()); break;
        case DownloadTaskStatus::Paused: statusText = tr("任务暂停：%1").arg(task->fileName()); break;
        case DownloadTaskStatus::Cancelled: statusText = tr("任务取消：%1").arg(task->fileName()); break;
        case DownloadTaskStatus::Completed: statusText = tr("任务完成：%1").arg(task->fileName()); break;
        case DownloadTaskStatus::Failed: statusText = tr("任务失败：%1").arg(task->fileName()); break;
        default: statusText = tr("未知状态：%1").arg(task->fileName()); break;
        }
        
        LOGD(QString("更新状态栏消息:%1").arg(statusText));
        ui->statusbar->showMessage(statusText);
        LOGD("状态栏消息更新完成");
    } else {
        LOGD("sender不是有效的DownloadTask对象");
    }
    
    LOGD("任务状态变更处理完成");
}

void MainWindow::onTaskProgressUpdated(qint64 bytesReceived, qint64 totalBytes, qint64 speed)
{
    DownloadTask* raw = qobject_cast<DownloadTask*>(sender());
    QPointer<DownloadTask> safeTask(raw);
    if (safeTask) {
        DownloadTask* task = safeTask.data();
        // 进度日志节流：每 10% 记录一次（用成员 m_lastLoggedProgress 替代原来的 static QHash，
        // 避免 static 跨实例共享并防止退出时泄漏）。
        int currentProgress = task->progressPercentage();

        QPointer<DownloadTask> key(task);
        qint64 last = m_lastLoggedProgress.value(key, -1);
        if (last < 0 || currentProgress - last >= 10) {
            LOGD(QString("任务进度更新 - 文件:%1 进度:%2% 已接收:%3 总大小:%4 速度:%5")
                 .arg(task->fileName())
                 .arg(currentProgress)
                 .arg(bytesReceived)
                 .arg(totalBytes)
                 .arg(speed));
            m_lastLoggedProgress.insert(key, currentProgress);
        }

        // 收到进度回调时确保定时器是运行的（防止上面 stop 后没有重新启动）
        {
            QMutexLocker locker(&m_tableMutex);
            m_tasksToUpdate.insert(safeTask);
        }
        if (!m_uiUpdateTimer->isActive()) {
            m_uiIdleTicks = 0;
            m_uiUpdateTimer->start(200);
        }
    }
}

void MainWindow::onTaskFinished()
{
    LOGD("接收到任务完成信号");
    
    DownloadTask* task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        LOGD(QString("任务完成 - 文件:%1 最终状态:%2").arg(task->fileName()).arg(static_cast<int>(task->status())));
        
        LOGD("更新表格中的任务状态");
        updateTaskInTable(task); // 确保最终状态更新
        LOGD("表格状态更新完成");
        
        if (task->status() == DownloadTaskStatus::Completed) {
            LOGD("任务成功完成，显示成功通知");
            showSystemNotification(tr("下载完成"), tr("文件 '%1' 已下载完成！").arg(task->fileName()), QSystemTrayIcon::Information);
        } else if (task->status() == DownloadTaskStatus::Failed) {
            LOGD("任务失败，显示失败通知");
            showSystemNotification(tr("下载失败"), tr("文件 '%1' 下载失败！").arg(task->fileName()), QSystemTrayIcon::Warning);
        } else if (task->status() == DownloadTaskStatus::Cancelled) {
            LOGD("任务取消，显示取消通知");
            showSystemNotification(tr("下载取消"), tr("文件 '%1' 下载已取消。").arg(task->fileName()), QSystemTrayIcon::Information);
        }
        
        LOGD("系统通知已显示");
        // 任务完成后，DownloadManager会负责deleteLater，这里不需要removeTaskFromTable
        // removeTaskFromTable(task);
    } else {
        LOGD("sender不是有效的DownloadTask对象");
    }
    
    LOGD("任务完成处理完成");
}

void MainWindow::onTaskError(const QString& errorString)
{
    LOGD(QString("接收到任务错误信号，错误信息:%1").arg(errorString));
    
    DownloadTask* task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        LOGD(QString("任务错误 - 文件:%1 错误:%2").arg(task->fileName()).arg(errorString));
        
        LOGD("更新表格中的任务状态为失败");
        updateTaskInTable(task); // 确保状态更新为失败
        LOGD("表格状态更新完成");
        
        LOGD("显示错误系统通知");
        showSystemNotification(tr("下载错误"), tr("文件 '%1' 下载出错：%2").arg(task->fileName()).arg(errorString), QSystemTrayIcon::Critical);
        LOGD("系统通知已显示");
        
        LOGD("更新状态栏错误消息");
        ui->statusbar->showMessage(tr("任务错误：%1 - %2").arg(task->fileName()).arg(errorString));
        LOGD("状态栏消息更新完成");
    } else {
        LOGD("sender不是有效的DownloadTask对象");
    }
    
    LOGD("任务错误处理完成");
}

void MainWindow::onThemeChanged(const QString& themeName)
{
    loadStyleSheet(themeName);
}

void MainWindow::on_actionExit_triggered()
{
    // File->Exit 必须真的退出程序：标记 m_quitting，让 closeEvent 接受关闭事件。
    // 默认 Qt Designer 把 actionExit 自动连接到 close()，会先被 closeEvent 拦截成"最小化到托盘"。
    // 由本槽显式接管：先标记再调用 close()，从而保证退出意图生效。
    m_quitting = true;
    close();
}

void MainWindow::refreshSelectionActionStates()
{
    // 根据当前选中的任务状态启用/禁用 暂停/恢复/取消 按钮：
    // 选中至少一个 Downloading 任务 → Pause 启用；
    // 选中至少一个 Paused 任务       → Resume 启用；
    // 选中至少一个可取消（Downloading/Paused/Pending/Failed）的任务 → Cancel 启用。
    if (!ui || !ui->tableWidget) return;
    const QList<QTableWidgetItem*> selected = ui->tableWidget->selectedItems();
    bool canPause = false, canResume = false, canCancel = false;
    QSet<int> seen;
    for (QTableWidgetItem* it : selected) {
        int row = it->row();
        if (seen.contains(row)) continue;
        seen.insert(row);
        QTableWidgetItem* col0 = ui->tableWidget->item(row, 0);
        if (!col0) continue;
        DownloadTask* task = col0->data(Qt::UserRole).value<DownloadTask*>();
        if (!task) continue;
        switch (task->status()) {
            case DownloadTaskStatus::Downloading: canPause = true; canCancel = true; break;
            case DownloadTaskStatus::Paused:      canResume = true; canCancel = true; break;
            case DownloadTaskStatus::Pending:     canCancel = true; break;
            case DownloadTaskStatus::Failed:      canCancel = true; break;
            default: break;
        }
    }
    if (ui->actionPauseSelected)   ui->actionPauseSelected->setEnabled(canPause);
    if (ui->actionResumeSelected)  ui->actionResumeSelected->setEnabled(canResume);
    if (ui->actionCancelSelected)  ui->actionCancelSelected->setEnabled(canCancel);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 如果是程序主动退出（托盘退出/File->Exit），直接接受关闭事件
    if (m_quitting) {
        event->accept();
        return;
    }

    // 只有当存在正在下载/暂停的任务时，才允许最小化到托盘；
    // 否则按"用户关闭窗口"直接退出，避免历史遗留"按 X 永远不退出"的体验。
    bool hasActive = false;
    if (ui && ui->tableWidget) {
        QMutexLocker locker(&m_tableMutex);
        for (int row = 0; row < ui->tableWidget->rowCount(); ++row) {
            QTableWidgetItem* col0 = ui->tableWidget->item(row, 0);
            if (!col0) continue;
            DownloadTask* task = col0->data(Qt::UserRole).value<DownloadTask*>();
            if (task) {
                const auto st = task->status();
                if (st == DownloadTaskStatus::Downloading || st == DownloadTaskStatus::Paused) {
                    hasActive = true; break;
                }
            }
        }
    }

    if (hasActive && m_systemTray && m_systemTray->isVisible()) {
        hide(); // 隐藏主窗口
        showSystemNotification(tr("下载器正在后台运行"), tr("点击图标可恢复窗口。"), QSystemTrayIcon::Information);
        event->ignore(); // 忽略关闭事件，阻止程序退出
    } else {
        // 没有活跃任务或没有托盘，按 X 就直接退出
        event->accept();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    
    // 窗口大小改变时，确保表格列宽合理分配
    if (ui->tableWidget && ui->tableWidget->columnCount() > 0) {
        int totalWidth = ui->tableWidget->viewport()->width();
        
        // 计算固定列的总宽度
        int fixedWidth = ui->tableWidget->columnWidth(2) + ui->tableWidget->columnWidth(6); // 进度条 + 操作列
        int contentWidth = 0;
        
        // 计算内容自适应列的宽度
        for (int i = 3; i <= 5; ++i) {
            contentWidth += ui->tableWidget->columnWidth(i);
        }
        
        // 剩余宽度分配给文件名和URL列
        int remainingWidth = totalWidth - fixedWidth - contentWidth - 20; // 预留边距
        if (remainingWidth > 0) {
            // 文件名列占30%，URL列占70%
            int fileNameWidth = qMax(120, static_cast<int>(remainingWidth * 0.3));
            int urlWidth = qMax(200, remainingWidth - fileNameWidth);
            
            // 限制最大宽度
            fileNameWidth = qMin(fileNameWidth, 300);
            urlWidth = qMin(urlWidth, 500);
            
            ui->tableWidget->setColumnWidth(0, fileNameWidth);
        }
    }
}

void MainWindow::on_actionChinese_triggered()
{
    switchLanguage("zh_CN");
}

void MainWindow::on_actionEnglish_triggered()
{
    switchLanguage("en_US");
}

void MainWindow::switchLanguage(const QString& language)
{
    // 尝试加载新翻译，失败时不要破坏当前已安装的翻译器
    // QTranslator 禁用了拷贝/移动赋值，所以用 unique_ptr 管理新翻译器，
    // 加载成功后转移所有权到 m_translator 指针。
    std::unique_ptr<QTranslator> newTranslator(new QTranslator());
    bool loaded = false;
    QString loadedFrom;

    auto tryLoad = [&newTranslator, &loaded, &loadedFrom](const QString& path) -> bool {
        if (newTranslator->load(path)) {
            loaded = true;
            loadedFrom = path;
            return true;
        }
        return false;
    };

    if (language == "zh_CN") {
        loaded = tryLoad(":/translations/zh_CN.qm");
        if (!loaded) {
            QString appDir = QCoreApplication::applicationDirPath();
            loaded = tryLoad(appDir + "/translations/zh_CN.qm");
        }
        if (!loaded) {
            loaded = tryLoad("translations/zh_CN.qm");
        }
    } else if (language == "en_US") {
        loaded = tryLoad(":/translations/en_US.qm");
        if (!loaded) {
            QString appDir = QCoreApplication::applicationDirPath();
            loaded = tryLoad(appDir + "/translations/en_US.qm");
        }
        if (!loaded) {
            loaded = tryLoad("translations/en_US.qm");
        }
    }

    if (loaded) {
        // 加载成功才替换：先移除旧翻译器，删除旧对象，再安装新翻译器
        if (m_translator) {
            qApp->removeTranslator(m_translator);
            delete m_translator;
            m_translator = nullptr;
        }
        m_translator = newTranslator.release();
        qApp->installTranslator(m_translator);
        m_currentLanguage = language;
        qDebug() << "Switched to" << language << "from" << loadedFrom;
    } else {
        qWarning() << "Failed to load translation for" << language;
    }

    // 重新翻译UI
    ui->retranslateUi(this);

    // 重新设置表头标签
    ui->tableWidget->setHorizontalHeaderLabels({
        tr("文件名"), tr("URL"), tr("进度"), tr("大小"), tr("速度"), tr("状态"), tr("操作")
    });

    // 更新语言菜单状态
    updateLanguageMenu();

    // 显示切换成功消息
    if (language == "zh_CN") {
        ui->statusbar->showMessage(tr("已切换到中文界面"));
    } else if (language == "en_US") {
        ui->statusbar->showMessage(tr("Switched to English interface"));
    }
}

/**
 * @brief 更新语言菜单状态
 *
 * 根据当前界面语言设置语言菜单项的选中状态。
 * 统一使用 m_currentLanguage 成员变量，不再依赖窗口标题字符串匹配。
 */
void MainWindow::updateLanguageMenu()
{
    if (ui->actionChinese) {
        ui->actionChinese->setChecked(m_currentLanguage == "zh_CN");
    }
    if (ui->actionEnglish) {
        ui->actionEnglish->setChecked(m_currentLanguage == "en_US");
    }
}

/**
 * @brief UI更新定时器超时处理
 * 
 * 每200毫秒触发一次，用于批量更新任务状态：
 * 1. 遍历m_tasksToUpdate集合中的所有任务
 * 2. 调用updateTaskInTable更新每个任务的UI状态
 * 3. 在状态栏显示最后一个活动任务的信息
 * 4. 清空任务集合准备下一轮更新
 * 
 * 这种批量更新机制可以显著减少频繁UI更新带来的性能开销
 */
void MainWindow::onUiUpdateTimerTimeout()
{
    // 取出本轮需要刷新的任务，复制出来以避免与 producer（progressUpdated 等信号）的写入并发。
    // 用 QPointer 是因为 task 可能在 set 取出到 update 之间被 deleteLater（task 完成/取消路径）。
    QSet<QPointer<DownloadTask>> snapshot;
    {
        QMutexLocker locker(&m_tableMutex);
        if (m_tasksToUpdate.isEmpty()) {
            if (++m_uiIdleTicks >= 3 && m_uiUpdateTimer->isActive()) {
                m_uiUpdateTimer->stop();
                m_uiIdleTicks = 3;
            }
            return;
        }
        snapshot = m_tasksToUpdate;
        m_tasksToUpdate.clear();
    }
    m_uiIdleTicks = 0;

    // 批量更新；任务可能在 update 期间被销毁，updateTaskInTable 自身需有 QPointer 守卫。
    DownloadTask* lastActiveTask = nullptr;
    for (const QPointer<DownloadTask>& p : std::as_const(snapshot)) {
        DownloadTask* task = p.data();
        if (!task) continue; // 已 deleteLater，跳过
        updateTaskInTable(task);
        if (task->status() == DownloadTaskStatus::Downloading) {
            lastActiveTask = task;
        }
    }

    // 更新状态栏（只显示最后一个活动任务的信息）
    if (lastActiveTask) {
        ui->statusbar->showMessage(tr("下载中：%1 - %2% (%3/s)")
                                   .arg(lastActiveTask->fileName())
                                   .arg(lastActiveTask->progressPercentage())
                                   .arg(formatSpeed(lastActiveTask->downloadSpeed())));
    }
}

/**
 * @brief 配置响应式表格列宽
 * 
 * 根据内容重要性和显示需求动态调整表格列宽：
 * 1. 文件名列：固定宽度150px，可手动调整(120-300px)
 * 2. URL列：自动拉伸填充剩余空间，最小200px
 * 3. 进度列：固定宽度120px
 * 4. 大小/速度/状态列：根据内容自适应宽度(80-120px)
 * 5. 操作列：固定宽度80px
 * 
 * 同时设置表格基本属性：
 * - 启用自动换行和文本省略
 * - 设置合理的行高和网格样式
 * - 限制列宽在合理范围内
 */
void MainWindow::setupTableBasicProperties()
{
    QHeaderView* header = ui->tableWidget->horizontalHeader();
    
    // 设置表格基本属性
    ui->tableWidget->setWordWrap(true);
    ui->tableWidget->setTextElideMode(Qt::ElideMiddle);
    header->setStretchLastSection(false);
    
    // 优化表格外观
    ui->tableWidget->setGridStyle(Qt::SolidLine);
    ui->tableWidget->setShowGrid(true);
    header->setHighlightSections(false);
    header->setSectionsMovable(false);
    
    // 设置行高
    ui->tableWidget->verticalHeader()->setDefaultSectionSize(36);
    ui->tableWidget->verticalHeader()->setMinimumSectionSize(32);
    ui->tableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    
    // 设置选择行为和焦点策略
    ui->tableWidget->setFocusPolicy(Qt::StrongFocus);
    ui->tableWidget->setMouseTracking(true);
}

void MainWindow::configureColumnWidths()
{
    QHeaderView* header = ui->tableWidget->horizontalHeader();
    const int minWidths[] = {120, 200, 120, 80, 80, 80, 80};
    const int maxWidths[] = {300, 400, 150, 120, 120, 100, 100};
    
    // 固定宽度列
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(2, 120);
    
    header->setSectionResizeMode(6, QHeaderView::Fixed);
    ui->tableWidget->setColumnWidth(6, 80);
    
    // 自适应内容列
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    
    // 设置最小宽度
    for (int i = 3; i <= 5; ++i) {
        header->setMinimumSectionSize(minWidths[i]);
        if (ui->tableWidget->columnWidth(i) < minWidths[i]) {
            ui->tableWidget->setColumnWidth(i, minWidths[i]);
        }
    }
    
    // 文件名列
    header->setSectionResizeMode(0, QHeaderView::Interactive);
    ui->tableWidget->setColumnWidth(0, 150);
    header->setMinimumSectionSize(minWidths[0]);
    
    // URL列
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    if (ui->tableWidget->columnWidth(1) < minWidths[1]) {
        ui->tableWidget->setColumnWidth(1, minWidths[1]);
    }
}

void MainWindow::setupHeaderBehavior()
{
    connect(ui->tableWidget->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logicalIndex, int oldSize, int newSize) {
        Q_UNUSED(oldSize)

        const int minWidths[] = {120, 200, 120, 80, 80, 80, 80};
        const int maxWidths[] = {300, 400, 150, 120, 120, 100, 100};

        if (logicalIndex < 0 || logicalIndex >= 7) {
            return;
        }

        // URL 列（索引1）特殊处理：固定一个最小宽度，不让用户拖到 0
        if (logicalIndex == 1) {
            if (newSize < minWidths[1]) {
                ui->tableWidget->setColumnWidth(1, minWidths[1]);
            }
            // URL 列是 Stretch 模式，不强制上限
            return;
        }

        if (newSize < minWidths[logicalIndex]) {
            ui->tableWidget->setColumnWidth(logicalIndex, minWidths[logicalIndex]);
        } else if (newSize > maxWidths[logicalIndex]) {
            ui->tableWidget->setColumnWidth(logicalIndex, maxWidths[logicalIndex]);
        }
    });
}

void MainWindow::setupResponsiveTableColumns()
{
    setupTableBasicProperties();
    configureColumnWidths();
    setupHeaderBehavior();
}

/**
 * @brief 处理定时下载任务触发
 * @param task 触发的定时任务
 *
 * 当定时任务到达预定时间时，自动创建并开始下载任务。
 * 注意：schedulemanager.cpp 在遍历 m_tasks 期间触发了此槽，
 * 这里先按值拷贝一份本地副本，避免直接引用 m_tasks 中的元素。
 */
void MainWindow::onScheduledTaskTriggered(const ScheduledTask& task)
{
    // 先做值拷贝，避免 schedule 内部迭代与触发并发（schedulemanager.cpp 在迭代中触发此槽）
    const ScheduledTask taskCopy = task;

    LOGD(QString("定时任务触发 - 文件:%1 URL:%2").arg(taskCopy.fileName).arg(taskCopy.url));

    // 创建下载任务
    DownloadTask* downloadTask = m_downloadManager.createTask(QUrl(taskCopy.url), taskCopy.savePath, 4);
    if (downloadTask) {
        // 开始下载
        m_downloadManager.startTask(downloadTask);

        // 显示系统通知
        showSystemNotification(tr("定时下载开始"),
                              tr("文件 '%1' 定时下载已开始").arg(taskCopy.fileName),
                              QSystemTrayIcon::Information);

        LOGD(QString("定时下载任务已开始 - 文件:%1").arg(taskCopy.fileName));
    } else {
        LOGD(QString("定时下载任务创建失败 - 文件:%1 URL:%2").arg(taskCopy.fileName).arg(taskCopy.url));

        showSystemNotification(tr("定时下载失败"),
                              tr("文件 '%1' 定时下载失败").arg(taskCopy.fileName),
                              QSystemTrayIcon::Warning);
    }
}

/**
 * @brief 处理定时任务列表改变
 * 
 * 当定时任务列表发生变化时更新相关UI
 */
void MainWindow::onScheduledTasksChanged()
{
    // 可以在这里更新定时任务列表显示
    LOGD("定时任务列表已更新");
}

/**
 * @brief 处理定时下载按钮点击事件
 */
void MainWindow::on_actionScheduleTask_triggered()
{
    LOGD("定时下载按钮被点击");
    
    // 创建定时下载对话框
    ScheduleDialog scheduleDialog(this);
    
    if (scheduleDialog.exec() == QDialog::Accepted) {
        // 获取用户输入的URL和保存路径
        QString url = scheduleDialog.url();
        QString savePath = scheduleDialog.savePath();
        
        if (!url.isEmpty() && !savePath.isEmpty()) {
            // 创建定时任务
            ScheduledTask task;
            task.fileName = QFileInfo(savePath).fileName();
            task.url = url;
            task.savePath = savePath;
            task.isRepeat = scheduleDialog.isRepeat();
            task.repeatInterval = scheduleDialog.repeatInterval();
            task.isActive = true;
            task.type = "scheduled_download"; // 标识为定时下载任务
            
            // 根据定时类型设置开始时间
            ScheduleDialog::ScheduleType type = scheduleDialog.scheduleType();
            if (type == ScheduleDialog::Immediate) {
                task.scheduledTime = QDateTime::currentDateTime();
            } else if (type == ScheduleDialog::SpecificTime) {
                task.scheduledTime = scheduleDialog.startTime();
            } else if (type == ScheduleDialog::Delayed) {
                task.scheduledTime = QDateTime::currentDateTime().addSecs(scheduleDialog.delayMinutes() * 60);
            }
            
            // 添加定时任务
            ScheduleManager::instance()->addScheduledTask(task);
            
            // 显示成功消息
            QString message;
            if (type == ScheduleDialog::Immediate) {
                message = tr("定时下载任务已创建，将立即开始下载");
            } else if (type == ScheduleDialog::SpecificTime) {
                message = tr("定时下载任务已创建，将在 %1 开始下载").arg(task.scheduledTime.toString("yyyy-MM-dd hh:mm:ss"));
            } else {
                message = tr("定时下载任务已创建，将在 %1 分钟后开始下载").arg(scheduleDialog.delayMinutes());
            }
            
            if (task.isRepeat) {
                message += tr("，每 %1 小时重复一次").arg(task.repeatInterval);
            }
            
            ui->statusbar->showMessage(message);
            showSystemNotification(tr("定时下载设置成功"), message, QSystemTrayIcon::Information);
            
            LOGD(QString("定时下载任务创建成功 - 文件:%1 时间:%2").arg(task.fileName).arg(task.scheduledTime.toString()));
        }
    }
}

/**
 * @brief 处理查看历史记录按钮点击事件
 */
void MainWindow::on_actionViewHistory_triggered()
{
    LOGD("查看历史记录按钮被点击");
    
    // 创建历史记录对话框
    HistoryDialog historyDialog(this);
    historyDialog.exec();
    
    LOGD("历史记录对话框关闭");
}