function Controller()
{
}

var thermVaneOverwriteHandled = {};

function normalizePath(path)
{
    return installer.fromNativeSeparators(path).replace(/\/+$/, "");
}

function targetContainsExistingInstallation(targetDir)
{
    var target = normalizePath(targetDir);
    if (target.length === 0) {
        return false;
    }

    return installer.fileExists(target + "/ThermVaneMaintenanceTool.exe")
        || installer.fileExists(target + "/ThermVaneMaintenanceTool.ini")
        || installer.fileExists(target + "/maintenancetool.exe")
        || installer.fileExists(target + "/components.xml");
}

function removeExistingInstallation(targetDir)
{
    var target = normalizePath(targetDir);
    if (target.length < 8 || /^\/?[A-Za-z]:\/?$/.test(target)) {
        QMessageBox.critical("ThermVane.InvalidOverwriteTarget",
            "ThermVane Installer",
            "Der ausgewählte Zielordner ist kein gültiger Installationsordner.",
            QMessageBox.Ok);
        return false;
    }

    if (!installer.hasAdminRights() && !installer.gainAdminRights()) {
        QMessageBox.critical("ThermVane.AdminRequiredForOverwrite",
            "ThermVane Installer",
            "Zum Überschreiben der bestehenden Installation werden Administratorrechte benötigt.",
            QMessageBox.Ok);
        return false;
    }

    var nativeTarget = installer.toNativeSeparators(target);
    var result = installer.execute("cmd.exe", ["/C", "rmdir /S /Q \"" + nativeTarget + "\""]);
    if (result.length < 2 || result[1] !== 0) {
        QMessageBox.critical("ThermVane.OverwriteFailed",
            "ThermVane Installer",
            "Die bestehende Installation konnte nicht entfernt werden:\n" + nativeTarget,
            QMessageBox.Ok);
        return false;
    }

    return true;
}

function askOverwriteExistingInstallation(targetDir)
{
    var target = normalizePath(targetDir);
    if (!targetContainsExistingInstallation(target) || thermVaneOverwriteHandled[target]) {
        return;
    }

    var answer = QMessageBox.question("ThermVane.OverwriteExistingInstallation",
        "ThermVane Installer",
        "Der ausgewählte Zielordner enthält bereits eine ThermVane-Installation.\n\n"
            + "Soll der vorhandene Ordner überschrieben werden?\n\n"
            + installer.toNativeSeparators(target),
        QMessageBox.Yes | QMessageBox.No,
        QMessageBox.No);

    if (answer === QMessageBox.Yes) {
        thermVaneOverwriteHandled[target] = removeExistingInstallation(target);
    }
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    var widget = gui.currentPageWidget();
    if (widget === null) {
        return;
    }

    askOverwriteExistingInstallation(widget.TargetDirectoryLineEdit.text);
    widget.TargetDirectoryLineEdit.editingFinished.connect(function() {
        askOverwriteExistingInstallation(widget.TargetDirectoryLineEdit.text);
    });
}
