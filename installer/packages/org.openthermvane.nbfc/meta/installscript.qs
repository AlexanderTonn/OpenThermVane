function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType !== "windows") {
        return;
    }

    component.addElevatedOperation("Execute",
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        "@TargetDir@/nbfc/install-nbfc.ps1",
        "@TargetDir@",
        "UNDOEXECUTE",
        "cmd", "/C",
        "if exist \"%ProgramFiles%\\NoteBook FanControl\\unins000.exe\" \"%ProgramFiles%\\NoteBook FanControl\\unins000.exe\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART");
}
