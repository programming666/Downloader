function Controller()
{
    installer.setDefaultPageVisible(QInstaller.Introduction, false);
    installer.setDefaultPageVisible(QInstaller.TargetDirectory, true);
    installer.setDefaultPageVisible(QInstaller.ComponentSelection, false);
    installer.setDefaultPageVisible(QInstaller.ReadyForInstallation, true);
    installer.setDefaultPageVisible(QInstaller.StartMenuSelection, true);
    installer.setDefaultPageVisible(QInstaller.LicenseCheck, true);
}

Controller.prototype.IntroductionPageCallback = function()
{
    var page = installer.pageWidgetByObjectName("IntroductionPage");
    page.setTitle("欢迎使用 Downloader 安装程序");
    page.setSubtitle("这个向导将引导您完成 Downloader 的安装过程。");
}

Controller.prototype.LicenseAgreementPageCallback = function()
{
    var page = installer.pageWidgetByObjectName("LicenseAgreementPage");
    page.selectAll();
}

// 卸载钩子：保留扩展点，便于后续清理注册表项 / 服务 / 用户配置等
// 当前实现为空，保持与历史版本一致；如有需要可在此添加自定义清理逻辑。
Controller.prototype.UninstallationCallback = function()
{
    if (installer.isUninstaller()) {
        // 占位：卸载时执行的清理逻辑（目前无操作）
        // console.log("Downloader uninstall started");
    }
}