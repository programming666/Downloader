#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QNetworkProxy>
#include <QCoreApplication> 
/**
 * @brief SettingsManager类用于管理应用程序的各种设置。
 * 这是一个单例类，负责加载、保存和提供对代理设置、主题、默认下载路径和默认线程数等配置的访问。
 */
class SettingsManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief 获取SettingsManager的单例实例。
     * @return SettingsManager的唯一实例。
     */
    static SettingsManager& instance();

    // 禁用拷贝构造函数和赋值运算符
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;

    /**
     * @brief 保存网络代理设置。
     * @param proxy 要保存的QNetworkProxy对象。
     */
    enum ProxyType {
        NoProxy = 0,
        HttpProxy,
        Socks5Proxy,
        SystemProxy
    };

    /**
     * @brief 保存网络代理设置。
     * @param type 代理类型
     * @param proxy 要保存的QNetworkProxy对象。
     */
    void saveProxy(ProxyType type, const QNetworkProxy& proxy);

    /**
     * @brief 加载网络代理设置。
     * @param type [out] 代理类型
     * @param proxy [out] 代理设置
     */
    void loadProxy(ProxyType& type, QNetworkProxy& proxy) const;

    /**
     * @brief 保存当前主题设置。
     * @param themeName 主题名称（例如："light"或"dark"）。
     */
    void saveTheme(const QString& themeName);

    /**
     * @brief 加载当前主题设置。
     * @return 主题名称。
     */
    QString loadTheme() const;

    /**
     * @brief 保存默认下载路径。
     * @param path 默认下载路径。
     */
    void saveDefaultDownloadPath(const QString& path);

    /**
     * @brief 加载默认下载路径。
     * @return 默认下载路径。
     */
    QString loadDefaultDownloadPath() const;

    /**
     * @brief 保存默认下载线程数。
     * @param threads 默认下载线程数。
     */
    void saveDefaultThreads(int threads);

    /**
     * @brief 加载默认下载线程数。
     * @return 默认下载线程数。
     */
    int loadDefaultThreads() const;

    /**
     * @brief 保存本地监听端口。
     * @param port 本地监听端口号。
     */
    void saveLocalListenPort(quint16 port);

    /**
     * @brief 加载本地监听端口
     * @return 返回本地监听端口号
     */
    quint16 loadLocalListenPort() const;

    /**
     * @brief 保存静默模式设置
     * @param silent 是否启用静默模式
     */
    void saveSilentMode(bool silent);

    /**
     * @brief 加载静默模式设置
     * @return 返回是否启用静默模式
     */
    bool loadSilentMode() const;

signals:
    /**
     * @brief 当主题发生改变时发射此信号。
     * @param themeName 新的主题名称。
     */
    void themeChanged(const QString& themeName);

private:
    /**
     * @brief 私有构造函数，确保单例模式。
     * @param parent 父QObject。
     */
    explicit SettingsManager(QObject *parent = nullptr);
    ~SettingsManager();

    QSettings* m_settings; ///< QSettings实例，用于实际的设置读写。

    // 设置键常量
    static const QString GROUP_NETWORK;
    static const QString KEY_PROXY_TYPE;
    static const QString KEY_PROXY_HOST;
    static const QString KEY_PROXY_PORT;
    static const QString KEY_PROXY_USER;
    static const QString KEY_PROXY_PASS;

    static const QString GROUP_UI;
    static const QString KEY_THEME;

    static const QString GROUP_DOWNLOAD;
    static const QString KEY_DEFAULT_PATH;
    static const QString KEY_DEFAULT_THREADS;

    static const QString GROUP_LOCAL_SERVER;
    static const QString KEY_LOCAL_LISTEN_PORT;

    static const QString GROUP_NOTIFICATION;
    static const QString KEY_SILENT_MODE;
};

#endif // SETTINGSMANAGER_H
