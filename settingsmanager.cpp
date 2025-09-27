#include "settingsmanager.h"
#include <QStandardPaths>
#include <QDir>
#include "logger.h"
// 初始化设置键常量
const QString SettingsManager::GROUP_NETWORK = "Network";
const QString SettingsManager::KEY_PROXY_TYPE = "ProxyType";
const QString SettingsManager::KEY_PROXY_HOST = "ProxyHost";
const QString SettingsManager::KEY_PROXY_PORT = "ProxyPort";
const QString SettingsManager::KEY_PROXY_USER = "ProxyUser";
const QString SettingsManager::KEY_PROXY_PASS = "ProxyPass";

const QString SettingsManager::GROUP_UI = "UI";
const QString SettingsManager::KEY_THEME = "Theme";

const QString SettingsManager::GROUP_DOWNLOAD = "Download";
const QString SettingsManager::KEY_DEFAULT_PATH = "DefaultDownloadPath";
const QString SettingsManager::KEY_DEFAULT_THREADS = "DefaultThreads";

const QString SettingsManager::GROUP_LOCAL_SERVER = "LocalServer";
const QString SettingsManager::KEY_LOCAL_LISTEN_PORT = "ListenPort";

const QString SettingsManager::GROUP_NOTIFICATION = "Notification";
const QString SettingsManager::KEY_SILENT_MODE = "SilentMode";

/**
 * @brief 设置管理器构造函数
 * 
 * 初始化应用程序组织名称和应用名称，确保设置存储在Windows注册表中
 * 创建QSettings实例用于读写应用程序配置，使用注册表存储设置
 * 
 * @param parent 父对象指针
 */
SettingsManager::SettingsManager(QObject *parent)
    : QObject(parent)
{
    // 设置应用程序名称和组织名称，确保注册表路径正确
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName("Downloader");
    }
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName("Programming666");
    }
    
    // 使用默认的QSettings构造函数，在Windows上会自动使用注册表存储
    // HKEY_CURRENT_USER\Software\Programming666\Downloader
    m_settings = new QSettings(this);
    LOGD("SettingsManager: 使用注册表存储设置");
}

SettingsManager::~SettingsManager()
{
    // QSettings的父对象是SettingsManager，SettingsManager的父对象是nullptr
    // 所以m_settings会在SettingsManager析构时自动删除，无需手动delete
}

SettingsManager& SettingsManager::instance()
{
    static SettingsManager instance;
    return instance;
}

/**
 * @brief 保存代理设置
 * 
 * 将代理类型和代理配置信息保存到设置文件中
 * 仅当代理类型不是系统代理时才保存具体的代理参数
 * 
 * @param type 代理类型（无代理/系统代理/手动代理）
 * @param proxy 具体的代理配置对象，包含主机名、端口、用户名、密码
 */
void SettingsManager::saveProxy(ProxyType type, const QNetworkProxy& proxy)
{
    m_settings->beginGroup(GROUP_NETWORK);
    m_settings->setValue(KEY_PROXY_TYPE, type);
    if (type != SystemProxy) {
        m_settings->setValue(KEY_PROXY_HOST, proxy.hostName());
        m_settings->setValue(KEY_PROXY_PORT, proxy.port());
        m_settings->setValue(KEY_PROXY_USER, proxy.user());
        m_settings->setValue(KEY_PROXY_PASS, proxy.password());
    }
    m_settings->endGroup();
}

/**
 * @brief 加载代理设置
 * 
 * 从设置文件中读取代理配置信息，填充代理类型和代理对象
 * 如果代理类型不是系统代理，则读取具体的代理参数
 * 
 * @param type 输出参数，返回代理类型
 * @param proxy 输出参数，返回具体的代理配置对象
 */
void SettingsManager::loadProxy(ProxyType& type, QNetworkProxy& proxy) const
{
    m_settings->beginGroup(GROUP_NETWORK);
    type = static_cast<ProxyType>(m_settings->value(KEY_PROXY_TYPE, NoProxy).toInt());
    if (type != SystemProxy) {
        proxy.setHostName(m_settings->value(KEY_PROXY_HOST).toString());
        proxy.setPort(m_settings->value(KEY_PROXY_PORT).toInt());
        proxy.setUser(m_settings->value(KEY_PROXY_USER).toString());
        proxy.setPassword(m_settings->value(KEY_PROXY_PASS).toString());
    }
    m_settings->endGroup();
}

/**
 * @brief 保存主题设置
 * 
 * 将主题名称保存到设置文件中，并发射themeChanged信号通知主题已改变
 * 支持动态主题切换，界面会立即应用新的主题样式
 * 
 * @param themeName 主题名称（如"light"、"dark"等）
 */
void SettingsManager::saveTheme(const QString& themeName)
{
    m_settings->beginGroup(GROUP_UI);
    m_settings->setValue(KEY_THEME, themeName);
    m_settings->endGroup();
    emit themeChanged(themeName); // 发射信号通知主题已改变
}

QString SettingsManager::loadTheme() const
{
    m_settings->beginGroup(GROUP_UI);
    QString theme = m_settings->value(KEY_THEME, "light").toString(); // 默认浅色主题
    m_settings->endGroup();
    return theme;
}

void SettingsManager::saveDefaultDownloadPath(const QString& path)
{
    m_settings->beginGroup(GROUP_DOWNLOAD);
    m_settings->setValue(KEY_DEFAULT_PATH, path);
    m_settings->endGroup();
}

/**
 * @brief 加载默认下载路径
 * 
 * 从设置中读取默认下载路径，提供智能的默认路径选择：
 * 1. 首选用户系统的标准下载目录
 * 2. 如果下载目录不存在，尝试创建目录
 * 3. 如果无法创建下载目录，回退到文档目录
 * 4. 确保返回的路径存在且有写入权限
 * 
 * @return 返回默认的下载路径，确保路径有效且可写入
 */
QString SettingsManager::loadDefaultDownloadPath() const
{
    m_settings->beginGroup(GROUP_DOWNLOAD);
    // 默认下载路径为用户下载目录
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (defaultPath.isEmpty()) {
        defaultPath = QDir::homePath() + "/Downloads"; // 备用方案
    }
    
    // 确保目录存在且有写入权限
    QDir dir(defaultPath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            // 如果无法创建下载目录，使用用户文档目录
            defaultPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
            if (defaultPath.isEmpty()) {
                defaultPath = QDir::homePath() + "/Documents";
            }
        }
    }
    
    QString path = m_settings->value(KEY_DEFAULT_PATH, defaultPath).toString();
    m_settings->endGroup();
    return path;
}

void SettingsManager::saveDefaultThreads(int threads)
{
    m_settings->beginGroup(GROUP_DOWNLOAD);
    m_settings->setValue(KEY_DEFAULT_THREADS, threads);
    m_settings->endGroup();
}

/**
 * @brief 加载默认线程数
 * 
 * 从设置中读取默认的下载线程数量，提供合理的默认值
 * 默认5个线程可以在下载速度和系统资源使用之间取得平衡
 * 
 * @return 返回默认的下载线程数量
 */
int SettingsManager::loadDefaultThreads() const
{
    m_settings->beginGroup(GROUP_DOWNLOAD);
    int threads = m_settings->value(KEY_DEFAULT_THREADS, 5).toInt(); // 默认5个线程
    m_settings->endGroup();
    return threads;
}

void SettingsManager::saveLocalListenPort(quint16 port)
{
    m_settings->beginGroup(GROUP_LOCAL_SERVER);
    m_settings->setValue(KEY_LOCAL_LISTEN_PORT, port);
    m_settings->endGroup();
}

quint16 SettingsManager::loadLocalListenPort() const
{
    m_settings->beginGroup(GROUP_LOCAL_SERVER);
    quint16 port = m_settings->value(KEY_LOCAL_LISTEN_PORT, 8080).toUInt(); // 默认端口8080
    m_settings->endGroup();
    return port;
}

void SettingsManager::saveSilentMode(bool silent)
{
    m_settings->beginGroup(GROUP_NOTIFICATION);
    m_settings->setValue(KEY_SILENT_MODE, silent);
    m_settings->endGroup();
}

bool SettingsManager::loadSilentMode() const
{
    m_settings->beginGroup(GROUP_NOTIFICATION);
    bool silent = m_settings->value(KEY_SILENT_MODE, false).toBool(); // 默认不启用静默模式
    m_settings->endGroup();
    return silent;
}
