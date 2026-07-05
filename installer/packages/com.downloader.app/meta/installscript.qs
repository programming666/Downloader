function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    
    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut", 
            "@TargetDir@/Downloader.exe", 
            "@DesktopDir@/Downloader.lnk",
            "iconPath=@TargetDir@/Downloader.exe");
            
        component.addOperation("CreateShortcut", 
            "@TargetDir@/Downloader.exe", 
            "@StartMenuDir@/Downloader.lnk",
            "iconPath=@TargetDir@/Downloader.exe");
    }
}