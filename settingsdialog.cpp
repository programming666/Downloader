#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settingsmanager.h"
#include "protocolregistrar.h"
#include "logger.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QUrl>

namespace {

// 验证代理 URL。允许 http/https/socks5 协议，且必须包含主机。
bool isValidProxyUrl(const QString &host, const QString &scheme)
{
    if (host.trimmed().isEmpty()) {
        return false;
    }
    // scheme 可能是 http / https / socks5 / socks5h
    static const QStringList allowed = {
        QStringLiteral("http"),
        QStringLiteral("https"),
        QStringLiteral("socks5"),
        QStringLiteral("socks5h")
    };
    if (!allowed.contains(scheme.toLower())) {
        return false;
    }
    // 用 QUrl 进一步验证主机是否合法
    QUrl u;
    u.setScheme(scheme.toLower());
    u.setHost(host.trimmed());
    if (!u.isValid() || u.host().isEmpty()) {
        return false;
    }
    return true;
}

// 在保存前执行所有校验；返回 true 表示通过。
bool validateSettingsDialogUi(Ui::SettingsDialog *ui)
{
    if (!ui) {
        return false;
    }

    // 端口 1..65535（QSpinBox 范围已限制，这里二次防护）
    const int proxyPort = ui->proxyPortSpinBox->value();
    if (proxyPort < 1 || proxyPort > 65535) {
        QMessageBox::warning(nullptr, QObject::tr("Invalid Input"),
                             QObject::tr("Proxy port must be in range 1..65535."));
        ui->proxyPortSpinBox->setFocus();
        return false;
    }

    const int listenPort = ui->localListenPortSpinBox->value();
    if (listenPort < 1 || listenPort > 65535) {
        QMessageBox::warning(nullptr, QObject::tr("Invalid Input"),
                             QObject::tr("Listen port must be in range 1..65535."));
        ui->localListenPortSpinBox->setFocus();
        return false;
    }

    // 线程数 1..32（夹紧）
    int threads = ui->defaultThreadsSpinBox->value();
    if (threads < 1) {
        threads = 1;
        ui->defaultThreadsSpinBox->setValue(threads);
    } else if (threads > 32) {
        threads = 32;
        ui->defaultThreadsSpinBox->setValue(threads);
    }

    // 代理：非 NoProxy 时主机必须存在且协议合法
    const int proxyTypeIndex = ui->proxyTypeComboBox->currentIndex();
    const int proxyTypeValue = ui->proxyTypeComboBox->itemData(proxyTypeIndex).toInt();
    if (proxyTypeValue != QNetworkProxy::NoProxy) {
        const QString host = ui->proxyHostLineEdit->text();
        QString scheme;
        switch (proxyTypeValue) {
        case QNetworkProxy::HttpProxy:
            scheme = QStringLiteral("http");
            break;
        case QNetworkProxy::Socks5Proxy:
            scheme = QStringLiteral("socks5");
            break;
        default:
            scheme = QStringLiteral("http");
            break;
        }
        if (!isValidProxyUrl(host, scheme)) {
            QMessageBox::warning(nullptr, QObject::tr("Invalid Input"),
                                 QObject::tr("Proxy host is invalid for the selected proxy type (%1). "
                                             "Only http, https, and socks5 schemes are accepted.")
                                     .arg(scheme));
            ui->proxyHostLineEdit->setFocus();
            return false;
        }
    }

    // 默认下载路径不能为空
    if (ui->defaultDownloadPathLineEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(nullptr, QObject::tr("Invalid Input"),
                             QObject::tr("Default download path cannot be empty."));
        ui->defaultDownloadPathLineEdit->setFocus();
        return false;
    }

    return true;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("设置"));

    // 初始化代理类型下拉框
    ui->proxyTypeComboBox->addItem(tr("不使用代理"), QNetworkProxy::NoProxy);
    ui->proxyTypeComboBox->addItem(tr("HTTP代理"), QNetworkProxy::HttpProxy);
    ui->proxyTypeComboBox->addItem(tr("SOCKS5代理"), QNetworkProxy::Socks5Proxy);
    // TODO: 添加系统代理选项

    // 初始化主题选择
    ui->themeComboBox->addItem(tr("浅色模式"), "light");
    ui->themeComboBox->addItem(tr("深色模式"), "dark");

    loadSettingsToUi();

    // Qt的自动连接机制会自动连接符合命名规则的槽函数
    // 不需要手动连接浏览按钮和代理类型下拉框，否则会导致信号被连接两次
    // connect(ui->browseButton, &QPushButton::clicked, this, &SettingsDialog::on_browseButton_clicked);
    // connect(ui->proxyTypeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::on_proxyTypeComboBox_currentIndexChanged);

    // 连接按钮框的信号 - 使用按钮框的标准信号
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        // OK 按钮：在接受对话框前先校验
        if (validateSettingsDialogUi(ui)) {
            saveSettingsFromUi();
            accept();
        }
    });
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);

    // 连接应用按钮的单独信号
    QPushButton* applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    if (applyButton) {
        connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::on_applyButton_clicked);
    }

    // 限制端口与线程数范围（与校验函数一致）
    ui->proxyPortSpinBox->setRange(1, 65535);
    ui->localListenPortSpinBox->setRange(1, 65535);
    ui->defaultThreadsSpinBox->setRange(1, 32);

    // 协议注册/反注册按钮（auto connect：方法名符合 on_<object>_<signal> 格式，但
    // 这里 on_xxx_clicked 用的是 _clicked 后缀，为了确定性我们手动 connect 一下）。
    connect(ui->registerProtocolButton, &QPushButton::clicked,
            this, &SettingsDialog::on_registerProtocolButton_clicked);
    connect(ui->unregisterProtocolButton, &QPushButton::clicked,
            this, &SettingsDialog::on_unregisterProtocolButton_clicked);

    // 初次打开对话框时把当前注册表状态刷新到 label
    refreshProtocolStatusUi();
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::loadSettingsToUi()
{
    // 加载代理设置
    SettingsManager::ProxyType proxyType;
    QNetworkProxy currentProxy;
    SettingsManager::instance().loadProxy(proxyType, currentProxy);
    
    int proxyTypeIndex = ui->proxyTypeComboBox->findData(proxyType);
    if (proxyTypeIndex != -1) {
        ui->proxyTypeComboBox->setCurrentIndex(proxyTypeIndex);
    }
    ui->proxyHostLineEdit->setText(currentProxy.hostName());
    ui->proxyPortSpinBox->setValue(currentProxy.port());
    ui->proxyUserLineEdit->setText(currentProxy.user());
    ui->proxyPassLineEdit->setText(currentProxy.password());
    on_proxyTypeComboBox_currentIndexChanged(ui->proxyTypeComboBox->currentIndex()); // 更新代理输入框状态

    // 加载默认下载路径
    ui->defaultDownloadPathLineEdit->setText(SettingsManager::instance().loadDefaultDownloadPath());

    // 加载默认线程数
    ui->defaultThreadsSpinBox->setValue(SettingsManager::instance().loadDefaultThreads());

    // 加载本地监听端口
    ui->localListenPortSpinBox->setValue(SettingsManager::instance().loadLocalListenPort());

    // 加载静默模式设置
    ui->silentModeCheckBox->setChecked(SettingsManager::instance().loadSilentMode());

    // 加载主题
    QString currentTheme = SettingsManager::instance().loadTheme();
    int themeIndex = ui->themeComboBox->findData(currentTheme);
    if (themeIndex != -1) {
        ui->themeComboBox->setCurrentIndex(themeIndex);
    }
}

void SettingsDialog::saveSettingsFromUi()
{
    // 防御性夹紧：即使外部绕过校验，也确保落盘值在合法范围
    int threads = ui->defaultThreadsSpinBox->value();
    if (threads < 1) threads = 1;
    if (threads > 32) threads = 32;

    int proxyPort = ui->proxyPortSpinBox->value();
    if (proxyPort < 1) proxyPort = 1;
    if (proxyPort > 65535) proxyPort = 65535;

    int listenPort = ui->localListenPortSpinBox->value();
    if (listenPort < 1) listenPort = 1;
    if (listenPort > 65535) listenPort = 65535;

    // 保存代理设置
    SettingsManager::ProxyType proxyType = static_cast<SettingsManager::ProxyType>(ui->proxyTypeComboBox->currentData().toInt());
    QNetworkProxy proxy;
    proxy.setType(static_cast<QNetworkProxy::ProxyType>(proxyType));
    proxy.setHostName(ui->proxyHostLineEdit->text().trimmed());
    proxy.setPort(static_cast<quint16>(proxyPort));
    proxy.setUser(ui->proxyUserLineEdit->text());
    proxy.setPassword(ui->proxyPassLineEdit->text());
    SettingsManager::instance().saveProxy(proxyType, proxy);

    // 保存默认下载路径
    SettingsManager::instance().saveDefaultDownloadPath(ui->defaultDownloadPathLineEdit->text().trimmed());

    // 保存默认线程数
    SettingsManager::instance().saveDefaultThreads(threads);

    // 保存本地监听端口
    SettingsManager::instance().saveLocalListenPort(static_cast<quint16>(listenPort));

    // 保存静默模式设置
    SettingsManager::instance().saveSilentMode(ui->silentModeCheckBox->isChecked());

    // 保存主题
    SettingsManager::instance().saveTheme(ui->themeComboBox->currentData().toString());

    QMessageBox::information(this, tr("设置已保存"), tr("应用程序设置已成功保存。"));
}

void SettingsDialog::on_browseButton_clicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("选择默认下载目录"),
                                                    ui->defaultDownloadPathLineEdit->text(),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isEmpty()) {
        ui->defaultDownloadPathLineEdit->setText(dir);
    }
}

void SettingsDialog::on_proxyTypeComboBox_currentIndexChanged(int index)
{
    QNetworkProxy::ProxyType type = static_cast<QNetworkProxy::ProxyType>(ui->proxyTypeComboBox->itemData(index).toInt());
    bool enableProxyFields = (type != QNetworkProxy::NoProxy); // 移除 SystemProxy 检查

    ui->proxyHostLineEdit->setEnabled(enableProxyFields);
    ui->proxyPortSpinBox->setEnabled(enableProxyFields);
    ui->proxyUserLineEdit->setEnabled(enableProxyFields);
    ui->proxyPassLineEdit->setEnabled(enableProxyFields);
}

void SettingsDialog::on_applyButton_clicked()
{
    if (validateSettingsDialogUi(ui)) {
        saveSettingsFromUi();
    }
}

void SettingsDialog::on_cancelButton_clicked()
{
    reject(); // 关闭对话框，不保存更改
}

void SettingsDialog::on_registerProtocolButton_clicked()
{
    LOGD("SettingsDialog: 注册 downloader:// 协议");
    if (!ProtocolRegistrar::registerWithCurrentExe()) {
        QMessageBox::warning(this, tr("注册失败"),
                             tr("无法写入注册表 (HKCU\\Software\\Classes\\downloader)。\n"
                                "请检查当前用户权限，或尝试以管理员权限运行 Downloader。"));
        refreshProtocolStatusUi();
        return;
    }
    SettingsManager::instance().saveProtocolRegistered(true);
    SettingsManager::instance().saveProtocolTargetPath(ProtocolRegistrar::currentExePath());
    QMessageBox::information(this, tr("注册成功"),
                             tr("downloader:// 协议已注册到当前程序。\n"
                                "现在可在浏览器/资源管理器地址栏粘贴:\n"
                                "  downloader://https://example.com/file.zip\n"
                                "来直接唤起下载器。"));
    refreshProtocolStatusUi();
}

void SettingsDialog::on_unregisterProtocolButton_clicked()
{
    LOGD("SettingsDialog: 取消注册 downloader:// 协议");
    if (!ProtocolRegistrar::unregister()) {
        QMessageBox::warning(this, tr("取消注册失败"),
                             tr("无法清空注册表项 (HKCU\\Software\\Classes\\downloader)。"));
        refreshProtocolStatusUi();
        return;
    }
    SettingsManager::instance().saveProtocolRegistered(false);
    QMessageBox::information(this, tr("已取消注册"),
                             tr("downloader:// 协议已从系统清空。"));
    refreshProtocolStatusUi();
}

void SettingsDialog::refreshProtocolStatusUi()
{
    if (!ui || !ui->protocolStatusLabel) {
        return;
    }
    const QString cmd = ProtocolRegistrar::registeredCommand();
    const QString registeredExe = SettingsManager::instance().loadProtocolTargetPath();
    const QString currentExe = ProtocolRegistrar::currentExePath();

    if (cmd.isEmpty()) {
        ui->protocolStatusLabel->setText(tr("状态: 未注册"));
    } else if (!registeredExe.isEmpty() && registeredExe != currentExe) {
        // 上次记录的 exe 与当前 exe 不一致（exe 搬家了）→ 提示用户重新注册
        ui->protocolStatusLabel->setText(tr("状态: 已注册，但目标 EXE 已变更（%1 → %2），请重新注册")
                                         .arg(registeredExe, currentExe));
    } else {
        ui->protocolStatusLabel->setText(tr("状态: 已注册 (%1)").arg(currentExe));
    }
}
