#include "protocolregistrar.h"
#include "logger.h"

#include <QCoreApplication>
#include <QSettings>

#ifdef _WIN32
#  include <windows.h>
#endif

namespace {

/// 单点定义 base 注册表路径，避免散落硬编码字符串。
/// 给 RegCreateKeyExW 用的 sub-path：只写不带 HKCU hive prefix 的 subkey 路径。
QString regSubpath()
{
    return QStringLiteral(R"(Software\Classes\)")
         + QString::fromLatin1(ProtocolRegistrar::kScheme);
}

/// 给 reg query 用的可读路径（HKEY_CURRENT_USER\Software\Classes\downloader）。
QString regBase()
{
    return QStringLiteral(R"(HKEY_CURRENT_USER\Software\Classes\)")
         + QString::fromLatin1(ProtocolRegistrar::kScheme);
}

/// 把 QString escape 给 QSettings (NativeFormat) 时，QSettings 自己处理
/// REG_SZ 转义（包括 %1 不需要额外的 % escape），这里 helper 留作
/// 未来调试/兼容性扩展。
QString escapeForNative(const QString& s) { return s; }

/// Windows 注册表写：(Default) 值到指定 subkey 完整路径下。
/// 不存在的中间父键会被 RegCreateKeyExW 自动创建（KEY_CREATE_SUB_KEY 权限），
/// 这是 QSettings NativeFormat 在路径含空格/连字符时偶尔不创建的可靠替代。
bool writeRegDefaultW(const QString& fullSubkey, const QString& value,
                      QString* errorOut)
{
#ifdef _WIN32
    const std::wstring wsub = fullSubkey.toStdWString();
    const std::wstring wval = value.toStdWString();
    HKEY hKey = nullptr;
    LONG rc = RegCreateKeyExW(
        HKEY_CURRENT_USER, wsub.c_str(),
        0, nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_WRITE,
        nullptr, &hKey, nullptr);
    if (rc != ERROR_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("RegCreateKeyExW failed: %1").arg(int(rc));
        return false;
    }
    rc = RegSetValueExW(
        hKey, L"", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(wval.c_str()),
        static_cast<DWORD>((wval.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("RegSetValueExW failed: %1").arg(int(rc));
        return false;
    }
    return true;
#else
    (void)fullSubkey; (void)value;
    if (errorOut) *errorOut = QStringLiteral("ProtocolRegistrar only supports Windows");
    return false;
#endif
}

/// Windows 注册表清空 (Default) 值。并不删除子键，仅清叶子。
bool clearRegDefaultW(const QString& fullSubkey, QString* errorOut)
{
#ifdef _WIN32
    const std::wstring wsub = fullSubkey.toStdWString();
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, wsub.c_str(), 0, KEY_WRITE, &hKey);
    if (rc == ERROR_FILE_NOT_FOUND) {
        // 路径不存在视为已清空，幂等成功
        return true;
    }
    if (rc != ERROR_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("RegOpenKeyExW failed: %1").arg(int(rc));
        return false;
    }
    // 把 (Default) 设为空字符串——Windows URL Protocol 解析要求 key 存在但 (Default) 可为空
    rc = RegSetValueExW(hKey, L"", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(L""), sizeof(wchar_t));
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("RegSetValueExW failed: %1").arg(int(rc));
        return false;
    }
    return true;
#else
    (void)fullSubkey;
    if (errorOut) *errorOut = QStringLiteral("ProtocolRegistrar only supports Windows");
    return false;
#endif
}

/// Windows 读 (Default) 值。
QString readRegDefaultW(const QString& fullSubkey)
{
#ifdef _WIN32
    const std::wstring wsub = fullSubkey.toStdWString();
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wsub.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[2048] = {};
    DWORD cbData = sizeof(buf);
    DWORD type = 0;
    LONG rc = RegQueryValueExW(hKey, L"", nullptr, &type,
                                reinterpret_cast<LPBYTE>(buf), &cbData);
    RegCloseKey(hKey);
    if (rc != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }
    return QString::fromWCharArray(buf);
#else
    (void)fullSubkey;
    return {};
#endif
}

} // namespace

QString ProtocolRegistrar::currentExePath()
{
    // QCoreApplication::applicationFilePath 在 QCoreApplication 构造后才有意义；
    // main.cpp 在 QApplication a(argc, argv); 之后才调用这里，所以总是有效。
    return QCoreApplication::applicationFilePath();
}

bool ProtocolRegistrar::registerWithCurrentExe()
{
    const QString exe = currentExePath();
    if (exe.isEmpty()) {
        LOGD("ProtocolRegistrar::registerWithCurrentExe: exe 路径为空（QCoreApplication 未构造？）");
        return false;
    }

    const QString base = regSubpath();   // Software\Classes\downloader (给 advapi32 用)
    QString err;

    // Windows 注册表 subkey 分隔符必须是 '\\'，Qt 写入时 QStringLiteral("URL Protocol") 也是
    // 子键名（含空格）。所有 path 用反斜杠。
    const QString subUrlProtocol  = base + QStringLiteral("\\URL Protocol");
    const QString subDefaultIcon  = base + QStringLiteral("\\DefaultIcon");
    const QString subShellCommand = base + QStringLiteral("\\shell\\open\\command");

    // (1) (Default) = "URL:Downloader Protocol"
    if (!writeRegDefaultW(base,
                          QString::fromLatin1(kFriendlyName), &err)) {
        LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 (Default) 失败 - %1").arg(err));
        return false;
    }
    LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 %1\\(Default) ok").arg(base));

    // (2) URL Protocol\(Default) = ""
    if (!writeRegDefaultW(subUrlProtocol, QString(), &err)) {
        LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 URL Protocol 失败 - %1").arg(err));
        return false;
    }
    LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 %1 ok").arg(subUrlProtocol));

    // (3) DefaultIcon\(Default) = "<exe>,0"
    if (!writeRegDefaultW(subDefaultIcon,
                          escapeForNative(exe + QStringLiteral(",0")), &err)) {
        LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 DefaultIcon 失败 - %1").arg(err));
        return false;
    }
    LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 %1 ok").arg(subDefaultIcon));

    // (4) shell\open\command\(Default) = "\"<exe>\" --open \"%1\""
    const QString cmd = QStringLiteral("\"") + exe
        + QStringLiteral("\" --open \"")
        + QString::fromLatin1(kUrlPlaceholder)
        + QStringLiteral("\"");
    if (!writeRegDefaultW(subShellCommand, cmd, &err)) {
        LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 command 失败 - %1").arg(err));
        return false;
    }
    LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 写 %1 ok, cmd=%2")
         .arg(subShellCommand, cmd));

    LOGD(QString("ProtocolRegistrar::registerWithCurrentExe: 成功注册 downloader:// → %1").arg(exe));
    return true;
}

bool ProtocolRegistrar::unregister()
{
    const QString base = regSubpath();
    QString err;
    bool anyFailed = false;

    auto clearDefault = [&anyFailed, &err](const QString& sub) {
        if (!clearRegDefaultW(sub, &err)) {
            LOGD(QString("ProtocolRegistrar::unregister: 清 %1 失败 - %2").arg(sub, err));
            anyFailed = true;
        } else {
            LOGD(QString("ProtocolRegistrar::unregister: 清 %1 ok").arg(sub));
        }
    };

    clearDefault(base);
    clearDefault(base + QStringLiteral("\\URL Protocol"));
    clearDefault(base + QStringLiteral("\\DefaultIcon"));
    clearDefault(base + QStringLiteral("\\shell\\open\\command"));

    LOGD(QString("ProtocolRegistrar::unregister: %1").arg(anyFailed ? "部分失败" : "成功"));
    return !anyFailed;
}

bool ProtocolRegistrar::isRegistered()
{
    const QString expectedCmd = QStringLiteral("\"") + currentExePath()
        + QStringLiteral("\" --open \"")
        + QString::fromLatin1(kUrlPlaceholder)
        + QStringLiteral("\"");
    const QString actual = readRegDefaultW(regSubpath() + QStringLiteral("\\shell\\open\\command"));
    return actual == expectedCmd;
}

QString ProtocolRegistrar::registeredCommand()
{
    return readRegDefaultW(regSubpath() + QStringLiteral("\\shell\\open\\command"));
}
