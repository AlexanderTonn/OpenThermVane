function Component()
{
}

function systemPowerShell()
{
    var root = installer.environmentVariable("WINDIR");
    if (root.length === 0) {
        root = installer.environmentVariable("SystemRoot");
    }
    return root.length === 0
        ? ""
        : installer.toNativeSeparators(root + "/System32/WindowsPowerShell/v1.0/powershell.exe");
}

function systemCommandProcessor()
{
    var root = installer.environmentVariable("WINDIR");
    if (root.length === 0) {
        root = installer.environmentVariable("SystemRoot");
    }
    return root.length === 0
        ? ""
        : installer.toNativeSeparators(root + "/System32/cmd.exe");
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType !== "windows") {
        return;
    }

    component.addElevatedOperation("Execute",
        systemPowerShell(),
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        "@TargetDir@/nbfc/install-nbfc.ps1",
        "@TargetDir@",
        "UNDOEXECUTE",
        systemCommandProcessor(), "/C",
        "if exist \"%ProgramFiles%\\NoteBook FanControl\\unins000.exe\" \"%ProgramFiles%\\NoteBook FanControl\\unins000.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART");
}
