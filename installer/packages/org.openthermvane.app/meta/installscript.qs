function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut",
            "@TargetDir@/bin/ThermVane.exe",
            "@StartMenuDir@/ThermVane.lnk",
            "workingDirectory=@TargetDir@/bin");
    }

    if (systemInfo.productType === "osx") {
        component.addElevatedOperation("Execute",
            "/bin/bash",
            "@TargetDir@/scripts/macos/install-fan-helper.sh",
            "@TargetDir@/bin/ThermVaneFanHelper",
            "UNDOEXECUTE",
            "/bin/bash",
            "@TargetDir@/scripts/macos/uninstall-fan-helper.sh");
    }
}
