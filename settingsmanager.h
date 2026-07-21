#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QObject>
#include <QSettings>
#include <QNetworkProxy>
#include <QCoreApplication>
#include <QString> 
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
     * @brief 保存用户选择的界面语言。
     *
     * 持久化为 "zh_CN" / "en_US" 等 BCP-47 风格 locale 字符串。保存时会通知
     * 任何监听 languageChanged() 的组件（如活动对话框、托盘）刷新字符串。
     * @param language 语言代码（"zh_CN" / "en_US" / 等）。
     */
    void saveLanguage(const QString& language);

    /**
     * @brief 读取上次保存的界面语言。
     * @return 语言代码；如果没有持久化值则返回空串，由调用方决定 fallback。
     */
    QString loadLanguage() const;

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

    /**
     * @brief 获取本地HTTP服务用于认证浏览器插件的bearer token。
     * @return 当前配置的token；如果未配置则生成并保存一个随机UUID token。
     */
    QString bearerToken();

    /**
     * @brief 设置bearer token。
     * @param token 要保存的token。
     */
    void setBearerToken(const QString& token);

    /**
     * @brief 加载自定义 URL Protocol 注册状态。
     * @return true 表示用户曾在本机注册过 downloader:// 协议；纯 UI 提示用，不强制以它为准。
     */
    bool loadProtocolRegistered() const;

    /**
     * @brief 保存自定义 URL Protocol 注册状态。
     * @param registered 是否注册。
     */
    void saveProtocolRegistered(bool registered);

    /**
     * @brief 加载注册过的 exe 路径（与当前 process 的 applicationFilePath 对比，
     *        若不一致提示用户重新注册）。
     * @return 注册时记录的 exe 路径；空表示未记录。
     */
    QString loadProtocolTargetPath() const;

    /**
     * @brief 保存注册时的 exe 路径。
     * @param path 完整 exe 路径。
     */
    void saveProtocolTargetPath(const QString& path);

    /**
     * @brief 执行版本迁移。检查版本键，对老版本schema应用必要的迁移。
     *
     * 每次写入时如果版本不匹配，会执行一次升级步骤。当前仅做占位，
     * 后续可以在此处补充键迁移逻辑。
     */
    void migrate();

signals:
    /**
     * @brief 当主题发生改变时发射此信号。
     * @param themeName 新的主题名称。
     */
    void themeChanged(const QString& themeName);

    /**
     * @brief 当语言发生改变时发射此信号。
     *
     * 已打开的对话框和动态 cell widget 应连接到该信号以重排字符串。
     * @param language 新生效的语言代码（"zh_CN" / "en_US" 等）。
     */
    void languageChanged(const QString& language);

    /**
     * @brief 当任意持久化设置（代理/线程数/默认路径/监听端口/静默模式等）
     * 通过 save*() 写入后发射此广播信号。
     *
     * 接收方需自行调用 load*() 拉取最新值；本信号不携带具体变更的 key，
     * 因为 Qt 中跨对象的信号开销极低，全量广播比按 key 细分更不易遗漏。
     */
    void settingsChanged();

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

    /**
     * @brief 用户选定的界面语言（"zh_CN" / "en_US"），持久化在 GROUP_UI 下；
     * 缺省返回空，调用方按系统区域决定 fallback。
     */
    static const QString KEY_LANGUAGE;

    static const QString GROUP_DOWNLOAD;
    static const QString KEY_DEFAULT_PATH;
    static const QString KEY_DEFAULT_THREADS;

    static const QString GROUP_LOCAL_SERVER;
    static const QString KEY_LOCAL_LISTEN_PORT;

    static const QString GROUP_NOTIFICATION;
    static const QString KEY_SILENT_MODE;

    static const QString GROUP_SECURITY;
    static const QString KEY_BEARER_TOKEN;

    static const QString GROUP_PROTOCOL;
    static const QString KEY_PROTOCOL_REGISTERED;
    static const QString KEY_PROTOCOL_TARGET_PATH;

    static const QString GROUP_META;
    static const QString KEY_SCHEMA_VERSION;

    /// 当前schema版本号。
    static constexpr int CURRENT_SCHEMA_VERSION = 1;
};

#endif // SETTINGSMANAGER_H
