#include "newtaskdialog.h"
#include "ui_newtaskdialog.h"
#include "settingsmanager.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QStyleFactory> // For QSS loading
#include <QFile>

/**
 * @brief 新建下载任务对话框构造函数
 * 
 * 初始化对话框界面，设置窗口标题为"新建下载任务"
 * 加载默认下载路径和线程数设置，限制线程数范围(1-114514，建议1-32)
 * 应用当前主题样式，连接按钮框的接受/拒绝信号
 * 
 * @param parent 父窗口指针，默认为nullptr
 */
NewTaskDialog::NewTaskDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::NewTaskDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("新建下载任务"));

    // 加载默认下载路径和线程数
    ui->savePathLineEdit->setText(SettingsManager::instance().loadDefaultDownloadPath());
    ui->threadCountSpinBox->setValue(SettingsManager::instance().loadDefaultThreads());

    // 限制线程数范围
    ui->threadCountSpinBox->setRange(1, 114514); // 建议限制在1-32之间

    // 应用当前主题样式
    applyTheme();

    // Qt的自动连接机制会自动连接符合命名规则的槽函数
    // 不需要手动连接浏览按钮，否则会导致信号被连接两次
    // connect(ui->browseButton, &QPushButton::clicked, this, &NewTaskDialog::on_browseButton_clicked);
    
    // 连接按钮框的信号（这些不会自动连接）
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &NewTaskDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &NewTaskDialog::reject);
}

NewTaskDialog::~NewTaskDialog()
{
    delete ui;
}

QString NewTaskDialog::url() const
{
    return ui->urlLineEdit->text();
}

QString NewTaskDialog::savePath() const
{
    return ui->savePathLineEdit->text();
}

int NewTaskDialog::threadCount() const
{
    return ui->threadCountSpinBox->value();
}

/**
 * @brief 应用主题样式
 * 
 * 获取当前主题名称，从Qt资源文件加载对应的QSS样式表
 * 如果加载成功则应用到对话框，失败时输出警告信息
 * 支持主题切换功能，确保界面风格一致性
 */
void NewTaskDialog::applyTheme()
{
    // 获取当前主题
    QString themeName = SettingsManager::instance().loadTheme();
    
    // 加载样式表
    QFile file(QString(":/styles/%1.qss").arg(themeName));
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = file.readAll();
        
        setStyleSheet(styleSheet);
        file.close();
        qDebug() << "NewTaskDialog loaded theme:" << themeName;
    } else {
        qWarning() << "NewTaskDialog failed to load stylesheet for theme:" << themeName << file.errorString();
    }
}

/**
 * @brief 浏览按钮点击事件处理
 * 
 * 打开目录选择对话框，让用户选择文件保存目录
 * 如果用户选择了有效目录，则更新保存路径输入框
 * 使用QFileDialog::getExistingDirectory提供用户友好的目录选择界面
 */
void NewTaskDialog::on_browseButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("选择保存目录"),
                                                    ui->savePathLineEdit->text(),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        ui->savePathLineEdit->setText(dir);
    }
}

/**
 * @brief 确认按钮点击事件处理
 * 
 * 验证用户输入的下载信息，包括URL和保存路径：
 * 1. 验证URL非空且格式有效
 * 2. 检查URL协议是否为HTTP/HTTPS
 * 3. 验证保存路径非空且目录存在
 * 4. 所有验证通过后才接受对话框
 * 
 * 验证失败时显示相应的警告信息并聚焦到对应输入框
 */
void NewTaskDialog::accept()
{
    QString url = ui->urlLineEdit->text().trimmed();
    QString savePath = ui->savePathLineEdit->text().trimmed();
    
    // 验证URL
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"), tr("URL不能为空"));
        ui->urlLineEdit->setFocus();
        return;
    }
    
    QUrl testUrl(url);
    if (!testUrl.isValid()) {
        QMessageBox::warning(this, tr("输入错误"), tr("URL格式无效：%1").arg(testUrl.errorString()));
        ui->urlLineEdit->setFocus();
        return;
    }
    
    // 检查URL是否支持HTTP/HTTPS协议
    if (testUrl.scheme() != "http" && testUrl.scheme() != "https") {
        QMessageBox::warning(this, tr("输入错误"), tr("不支持的协议：%1。仅支持HTTP和HTTPS协议").arg(testUrl.scheme()));
        ui->urlLineEdit->setFocus();
        return;
    }
    
    // 验证保存路径
    if (savePath.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"), tr("保存路径不能为空"));
        ui->savePathLineEdit->setFocus();
        return;
    }
    
    QDir dir(savePath);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("输入错误"), tr("保存路径不存在：%1").arg(savePath));
        ui->savePathLineEdit->setFocus();
        return;
    }
    
    QDialog::accept();
}
