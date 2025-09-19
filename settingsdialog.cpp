#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include "settingsmanager.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>

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
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    
    // 连接应用按钮的单独信号
    QPushButton* applyButton = ui->buttonBox->button(QDialogButtonBox::Apply);
    if (applyButton) {
        connect(applyButton, &QPushButton::clicked, this, &SettingsDialog::on_applyButton_clicked);
    }
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

    // 加载主题
    QString currentTheme = SettingsManager::instance().loadTheme();
    int themeIndex = ui->themeComboBox->findData(currentTheme);
    if (themeIndex != -1) {
        ui->themeComboBox->setCurrentIndex(themeIndex);
    }
}

void SettingsDialog::saveSettingsFromUi()
{
    // 保存代理设置
    SettingsManager::ProxyType proxyType = static_cast<SettingsManager::ProxyType>(ui->proxyTypeComboBox->currentData().toInt());
    QNetworkProxy proxy;
    proxy.setType(static_cast<QNetworkProxy::ProxyType>(proxyType));
    proxy.setHostName(ui->proxyHostLineEdit->text());
    proxy.setPort(ui->proxyPortSpinBox->value());
    proxy.setUser(ui->proxyUserLineEdit->text());
    proxy.setPassword(ui->proxyPassLineEdit->text());
    SettingsManager::instance().saveProxy(proxyType, proxy);

    // 保存默认下载路径
    SettingsManager::instance().saveDefaultDownloadPath(ui->defaultDownloadPathLineEdit->text());

    // 保存默认线程数
    SettingsManager::instance().saveDefaultThreads(ui->defaultThreadsSpinBox->value());

    // 保存本地监听端口
    SettingsManager::instance().saveLocalListenPort(ui->localListenPortSpinBox->value());

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
    saveSettingsFromUi();
}

void SettingsDialog::on_cancelButton_clicked()
{
    reject(); // 关闭对话框，不保存更改
}
