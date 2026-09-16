function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType !== "windows") {
        return;
    }

    var installerExe = "@TargetDir@/nbfc/nbfc-installer.exe";
    var installerMsi = "@TargetDir@/nbfc/nbfc-installer.msi";

    component.addElevatedOperation("Execute",
        "cmd", "/C",
        "if exist \"" + installerMsi + "\" msiexec /i \"" + installerMsi + "\" /qn /norestart & if exist \"" + installerExe + "\" \"" + installerExe + "\" /quiet /norestart",
        "UNDOEXECUTE",
        "cmd", "/C",
        "if exist \"" + installerMsi + "\" msiexec /x \"" + installerMsi + "\" /qn /norestart");
}
