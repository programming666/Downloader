#ifndef PROTOCOLREGISTRAR_H
#define PROTOCOLREGISTRAR_H

#include <QString>

/**
 * @brief 注册 `downloader://` URL Scheme 到 Windows 注册表（HKCU）。
 *
 * 注册位置：`HKEY_CURRENT_USER\Software\Classes\downloader\...`
 *   - (Default)                  = "URL:Downloader Protocol"
 *   - URL Protocol\(Default)     = ""
 *   - DefaultIcon\(Default)      = "<exe path>,0"
 *   - shell\open\command\(Default) = "\"<exe path>\" --open \"%1\""
 *
 * 使用 HKCU 而非 HKLM：HKCU 不需要管理员权限；卸载/重装不需要 UAC 提权。
 *
 * 写注册表用 `QSettings(NativeFormat)`，在 Qt 6 Windows 上不需要 advapi32 / Windoes API。
 * 读注册表同样用 QSettings（只读叶子的 (Default) 值）。
 *
 * unregister 第一版只清空 4 个 (Default) 值。注册表子键保留（Windows URL Protocol
 * 解析仍能 fallback 到 Default 检查），不再走 RegDeleteTreeW/advapi32 路径。
 */
class ProtocolRegistrar
{
public:
    /// scheme 名称。Windows URL Scheme 限制：字母数字 + 短横线 + 点。
    /// 这里用全小写单词，与浏览器 `/download` 风格一致。
    static constexpr const char* kScheme = "downloader";

    /// 应用展示名，会出现在"打开方式"对话框里。
    static constexpr const char* kFriendlyName = "URL:Downloader Protocol";

    /// 命令模板里的占位符（Windows 协议注册的标准 `%1`）。
    static constexpr const char* kUrlPlaceholder = "%1";

    /**
     * @brief 当前进程对应的 exe 路径（用 QCoreApplication::applicationFilePath）。
     * @return empty string 表示 QApplication 还没构造好。
     */
    static QString currentExePath();

    /**
     * @brief 注册 downloader:// 到 HKCU，指向 currentExePath()。
     * @return 成功 true；失败 false（QSettings status != NoError）
     */
    static bool registerWithCurrentExe();

    /**
     * @brief 取消注册：清空 4 个 (Default) 叶子。注册表子键保留。
     * @return 成功 true。
     */
    static bool unregister();

    /**
     * @brief 注册表里是否仍然指向 currentExePath()。
     * @return true 表示已注册且目标与当前 exe 一致。
     */
    static bool isRegistered();

    /**
     * @brief 已注册的命令模板字符串（用于 UI 显示）。
     * @return 已注册则返回 command 字符串（含 `%1` 占位符），否则空字符串。
     */
    static QString registeredCommand();
};

#endif // PROTOCOLREGISTRAR_H
