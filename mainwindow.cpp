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
#include <utility> // 用于std::as_const

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

    // 加载上次保存的主题
    loadStyleSheet(m_settingsManager.loadTheme());

    // 初始化语言菜单
    updateLanguageMenu();

    // 设置UI更新定时器
    connect(m_uiUpdateTimer, &QTimer::timeout, this, &MainWindow::onUiUpdateTimerTimeout);
    m_uiUpdateTimer->start(200); // 每200毫秒更新一次
}

MainWindow::~MainWindow()
{
    delete ui;
    // m_systemTray的父对象是MainWindow，会自动删除
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
    // 首先尝试从资源文件加载
    QFile file(QString(":/styles/%1.qss").arg(themeName));
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = file.readAll();
        qApp->setStyleSheet(styleSheet);
        file.close();
        qDebug() << "Loaded theme from resources:" << themeName;
        return;
    }
    
    // 如果资源文件加载失败，尝试从应用程序目录加载
    QString appDir = QCoreApplication::applicationDirPath();
    QFile absoluteFile(appDir + "/styles/" + themeName + ".qss");
    if (absoluteFile.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = absoluteFile.readAll();
        qApp->setStyleSheet(styleSheet);
        absoluteFile.close();
        qDebug() << "Loaded theme from application directory:" << themeName;
        return;
    }
    
    // 如果都失败了，尝试从当前工作目录加载
    QFile currentDirFile("styles/" + themeName + ".qss");
    if (currentDirFile.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = currentDirFile.readAll();
        qApp->setStyleSheet(styleSheet);
        currentDirFile.close();
        qDebug() << "Loaded theme from current directory:" << themeName;
        return;
    }
    
    qWarning() << "Failed to load stylesheet for theme:" << themeName << "tried all paths";
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
    QLabel* sizeLabel = new QLabel(formatBytes(task->downloadedSize()) + "/" + formatBytes(task->totalSize()), this);
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
                QString sizeText = formatBytes(task->downloadedSize()) + "/" + formatBytes(task->totalSize());
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
    if (m_systemTray) {
        m_systemTray->showMessage(title, message, icon);
    }
}

void MainWindow::onNewDownloadRequestFromBrowser(const QString& url, const QString& savePath)
{
    qDebug() << "Received download request from browser plugin. URL:" << url << "Save Path:" << savePath;
    // 这里可以弹出一个确认对话框，或者直接创建任务
    // 暂时直接创建任务
    QString finalSavePath = savePath.isEmpty() ? m_settingsManager.loadDefaultDownloadPath() + "/" + QUrl(url).fileName() : savePath;
    DownloadTask* task = m_downloadManager.createTask(url, finalSavePath, m_settingsManager.loadDefaultThreads());
    m_downloadManager.startTask(task);
    showSystemNotification(tr("新下载任务"), tr("已从浏览器插件接收到下载任务：%1").arg(QUrl(url).fileName()), QSystemTrayIcon::Information);
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
            QTimer::singleShot(100, this, [this, task]() {
                LOGD("QTimer 回调执行，开始启动任务");
                LOGD("调用 m_downloadManager.startTask");
                m_downloadManager.startTask(task);
                LOGD("startTask 调用完成");
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

#include <QProgressDialog>

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
    
    // 使用QtConcurrent实现并行暂停
    auto pauseOperation = [](DownloadTask* task) {
        if (task) task->pause();
    };
    
    // 创建并配置FutureWatcher
    QFutureWatcher<void>* watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::progressRangeChanged,
            m_pauseProgress, &QProgressDialog::setRange);
    connect(watcher, &QFutureWatcher<void>::progressValueChanged,
            m_pauseProgress, &QProgressDialog::setValue);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        if (m_pauseProgress) m_pauseProgress->accept();
        watcher->deleteLater();
    });
    
    // 启动并行任务
    QFuture<void> future = QtConcurrent::map(tasksToStop, pauseOperation);
    watcher->setFuture(future);
    
    // 连接取消信号
    connect(m_pauseProgress, &QProgressDialog::canceled, this, [watcher]() {
        watcher->cancel();
    });
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
        m_systemTray->showMessage(tr("新任务"), tr("已添加任务：%1").arg(task->fileName()), QSystemTrayIcon::Information, 3000);
        LOGD("系统托盘通知已显示");
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
    DownloadTask* task = qobject_cast<DownloadTask*>(sender());
    if (task) {
        // 为了避免日志过多，只在特定条件下记录进度日志
        static QHash<DownloadTask*, qint64> lastLoggedProgress;
        int currentProgress = task->progressPercentage();
        
        if (!lastLoggedProgress.contains(task) || 
            currentProgress - lastLoggedProgress[task] >= 10) { // 每10%记录一次
            LOGD(QString("任务进度更新 - 文件:%1 进度:%2% 已接收:%3 总大小:%4 速度:%5")
                 .arg(task->fileName())
                 .arg(currentProgress)
                 .arg(bytesReceived)
                 .arg(totalBytes)
                 .arg(speed));
            lastLoggedProgress[task] = currentProgress;
        }
        
        m_tasksToUpdate.insert(task);
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

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_systemTray && m_systemTray->isVisible()) {
        hide(); // 隐藏主窗口
        m_systemTray->showMessage(tr("下载器正在后台运行"), tr("点击图标可恢复窗口。"), QSystemTrayIcon::Information, 2000);
        event->ignore(); // 忽略关闭事件，阻止程序退出
    } else {
        // 如果没有托盘图标或者用户选择退出，则正常关闭
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
    static QTranslator translator;
    
    // 移除旧的翻译
    qApp->removeTranslator(&translator);
    
    // 加载新的翻译文件
    if (language == "zh_CN") {
        // 首先尝试从资源文件加载
        if (translator.load(":/translations/zh_CN.qm")) {
            qApp->installTranslator(&translator);
            qDebug() << "Switched to Chinese (from resource)";
        } else {
            // 如果资源文件加载失败，尝试从文件系统加载
            if (translator.load("translations/zh_CN.qm")) {
                qApp->installTranslator(&translator);
                qDebug() << "Switched to Chinese (from file system)";
            } else {
                qWarning() << "Failed to load Chinese translation";
            }
        }
    } else if (language == "en_US") {
        // 首先尝试从资源文件加载
        if (translator.load(":/translations/en_US.qm")) {
            qApp->installTranslator(&translator);
            qDebug() << "Switched to English (from resource)";
        } else {
            // 如果资源文件加载失败，尝试从文件系统加载
            if (translator.load("translations/en_US.qm")) {
                qApp->installTranslator(&translator);
                qDebug() << "Switched to English (from file system)";
            } else {
                qWarning() << "Failed to load English translation";
            }
        }
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
 * 根据当前界面语言设置语言菜单项的选中状态：
 * 1. 通过检查窗口标题判断当前语言(中文或英文)
 * 2. 设置对应语言菜单项的checked状态
 * 3. 确保每次语言切换后菜单状态同步更新
 */
void MainWindow::updateLanguageMenu()
{
    // 根据当前语言更新菜单项的选中状态
    QString currentLanguage = "zh_CN"; // 默认中文
    
    // 检查当前界面语言
    // 如果窗口标题是英文，说明当前是英文模式
    if (windowTitle().contains("Multi-thread")) {
        currentLanguage = "en_US";
    }
    
    // 直接通过ui指针访问动作，因为它们在ui_mainwindow.h中已声明
    if (ui->actionChinese) {
        ui->actionChinese->setChecked(currentLanguage == "zh_CN");
    }
    if (ui->actionEnglish) {
        ui->actionEnglish->setChecked(currentLanguage == "en_US");
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
    if (m_tasksToUpdate.isEmpty()) {
        return;
    }

    // 批量更新
    for (DownloadTask* task : std::as_const(m_tasksToUpdate)) {
        if (task) {
            updateTaskInTable(task);
        }
    }

    // 更新状态栏（只显示最后一个活动任务的信息）
    DownloadTask* lastActiveTask = nullptr;
    for (DownloadTask* task : std::as_const(m_tasksToUpdate)) {
        if (task && task->status() == DownloadTaskStatus::Downloading) {
            lastActiveTask = task;
        }
    }
    if (lastActiveTask) {
        ui->statusbar->showMessage(tr("下载中：%1 - %2% (%3/s)")
                                   .arg(lastActiveTask->fileName())
                                   .arg(lastActiveTask->progressPercentage())
                                   .arg(formatSpeed(lastActiveTask->downloadSpeed())));
    }

    m_tasksToUpdate.clear();
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
        
        if (logicalIndex < 7) {
            if (newSize < minWidths[logicalIndex]) {
                ui->tableWidget->setColumnWidth(logicalIndex, minWidths[logicalIndex]);
            } else if (newSize > maxWidths[logicalIndex]) {
                ui->tableWidget->setColumnWidth(logicalIndex, maxWidths[logicalIndex]);
            }
        }
    });
}

void MainWindow::setupResponsiveTableColumns()
{
    setupTableBasicProperties();
    configureColumnWidths();
    setupHeaderBehavior();
}