#include "newtaskdialog.h"
#include "ui_newtaskdialog.h"
#include "settingsmanager.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QStyleFactory> // For QSS loading
#include <QFile>
#include <QRegularExpression>
#include <QFileInfo>
#include <QEvent>

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

    // 不需要手动连接 buttonBox::accepted/rejected，setupUi 已通过 .ui 中的
    // <connection> 把 accepted 绑定到本类的 accept()，重写的 accept 会被调用。
}

NewTaskDialog::~NewTaskDialog()
{
    delete ui;
}

void NewTaskDialog::changeEvent(QEvent* event)
{
    if (event && event->type() == QEvent::LanguageChange) {
        // 应用翻译器变更：重写 .ui 中的字符串；再把动态文案（windowTitle）
        // 单独 setText 一次以保证覆盖。
        ui->retranslateUi(this);
        setWindowTitle(tr("新建下载任务"));
    }
    QDialog::changeEvent(event);
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
 * @brief 检查文件名是否安全（不包含危险字符或Windows保留名）
 *
 * 拒绝以下情况：
 *  - 空字符串
 *  - 包含 ".."（路径穿越）
 *  - 包含 NUL 或其他控制字符
 *  - Windows 保留名（CON/PRN/AUX/NUL/COM1-9/LPT1-9）
 *  - 以 '.' 或空格结尾
 *  - 包含 < > : " / \ | ? *
 *
 * @param name 待检查的文件名
 * @return 文件名安全返回 true，否则 false
 */
static bool isSafeFileName(const QString &name)
{
    if (name.isEmpty()) {
        return false;
    }

    // 路径穿越检测
    if (name.contains(QStringLiteral(".."))) {
        return false;
    }

    // 控制字符与 NUL
    for (QChar ch : name) {
        if (ch.isNull() || ch.category() == QChar::Other_Control) {
            return false;
        }
        // Windows 非法字符
        const char16_t cu = ch.unicode();
        if (cu == u'<' || cu == u'>' || cu == u':' || cu == u'"' ||
            cu == u'/' || cu == u'\\' || cu == u'|' || cu == u'?' || cu == u'*') {
            return false;
        }
    }

    // 不允许以 '.' 或空格结尾
    const QChar last = name.at(name.size() - 1);
    if (last == QLatin1Char('.') || last == QLatin1Char(' ')) {
        return false;
    }

    // Windows 保留名检测（不区分大小写、忽略扩展名）
    QString base = name;
    const int dotIdx = base.indexOf(QLatin1Char('.'));
    if (dotIdx > 0) {
        base = base.left(dotIdx);
    }
    const QString upper = base.toUpper();
    static const QStringList reserved = {
        QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
        QStringLiteral("NUL")
    };
    static const QStringList comPorts = {
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"),
        QStringLiteral("COM4"), QStringLiteral("COM5"), QStringLiteral("COM6"),
        QStringLiteral("COM7"), QStringLiteral("COM8"), QStringLiteral("COM9")
    };
    static const QStringList lptPorts = {
        QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3"),
        QStringLiteral("LPT4"), QStringLiteral("LPT5"), QStringLiteral("LPT6"),
        QStringLiteral("LPT7"), QStringLiteral("LPT8"), QStringLiteral("LPT9")
    };
    if (reserved.contains(upper) || comPorts.contains(upper) || lptPorts.contains(upper)) {
        return false;
    }

    return true;
}

/**
 * @brief 确认按钮点击事件处理
 *
 * 验证用户输入的下载信息，包括URL和保存路径：
 * 1. URL 必填且必须以 http:// 或 https:// 开头
 * 2. 保存路径必填、目录必须存在，且必须在配置的下载根目录内
 * 3. 文件名（从保存路径提取）必须通过 isSafeFileName 检查
 * 4. 所有验证通过后才接受对话框
 *
 * 验证失败时显示相应的警告信息并聚焦到对应输入框
 */
void NewTaskDialog::accept()
{
    QString url = ui->urlLineEdit->text().trimmed();
    QString savePath = ui->savePathLineEdit->text().trimmed();

    // 1) URL 必填
    if (url.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), tr("URL cannot be empty."));
        ui->urlLineEdit->setFocus();
        return;
    }

    // 2) URL 必须以 http:// 或 https:// 开头
    static const QRegularExpression urlRegex(QStringLiteral("^https?://.+"));
    if (!urlRegex.match(url).hasMatch()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("URL must start with http:// or https://"));
        ui->urlLineEdit->setFocus();
        return;
    }

    QUrl testUrl(url);
    if (!testUrl.isValid()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("URL is malformed: %1").arg(testUrl.errorString()));
        ui->urlLineEdit->setFocus();
        return;
    }

    if (testUrl.scheme() != QLatin1String("http") &&
        testUrl.scheme() != QLatin1String("https")) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Unsupported scheme: %1. Only HTTP and HTTPS are allowed.")
                                 .arg(testUrl.scheme()));
        ui->urlLineEdit->setFocus();
        return;
    }

    // 3) 保存路径必填且存在
    if (savePath.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"), tr("Save path cannot be empty."));
        ui->savePathLineEdit->setFocus();
        return;
    }

    QDir dir(savePath);
    if (!dir.exists()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Save path does not exist: %1").arg(savePath));
        ui->savePathLineEdit->setFocus();
        return;
    }

    // 4) 文件名安全检查
    const QString fileName = QFileInfo(savePath).fileName();
    if (fileName.isEmpty()) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Save path must include a file name."));
        ui->savePathLineEdit->setFocus();
        return;
    }
    if (!isSafeFileName(fileName)) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("File name is invalid or unsafe: %1").arg(fileName));
        ui->savePathLineEdit->setFocus();
        return;
    }

    // 5) 保存路径必须在配置的下载根目录内（防止任意目录写入）
    const QString configuredRoot = QDir::cleanPath(
        SettingsManager::instance().loadDefaultDownloadPath());
    const QString cleanSave = QDir::cleanPath(savePath);
    if (!configuredRoot.isEmpty() && !cleanSave.startsWith(configuredRoot)) {
        QMessageBox::warning(this, tr("Invalid Input"),
                             tr("Save path must be inside the configured download directory:\n%1")
                                 .arg(configuredRoot));
        ui->savePathLineEdit->setFocus();
        return;
    }

    QDialog::accept();
}
