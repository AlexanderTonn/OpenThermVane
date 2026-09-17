function Controller()
{
    installer.setMessageBoxAutomaticAnswer("OverwriteTargetDirectory", QMessageBox.Yes);
}

var thermVaneExistingTarget = "";
var thermVaneTargetPrepared = {};

function normalizePath(path)
{
    return installer.fromNativeSeparators(path).replace(/\/+$/, "");
}

function firstOutputLine(result)
{
    if (result.length < 2 || result[1] !== 0) {
        return "";
    }

    var output = String(result[0]).replace(/^\s+|\s+$/g, "");
    if (output.length === 0) {
        return "";
    }

    return normalizePath(output.split(/\r?\n/)[0]);
}

function directoryExists(path)
{
    var nativePath = installer.toNativeSeparators(normalizePath(path));
    var result = installer.execute("cmd.exe", ["/C", "if exist \"" + nativePath + "\\NUL\" (exit /B 0) else (exit /B 1)"]);
    return result.length >= 2 && result[1] === 0;
}

function fileExists(path)
{
    var nativePath = installer.toNativeSeparators(normalizePath(path));
    var result = installer.execute("cmd.exe", ["/C", "if exist \"" + nativePath + "\" (exit /B 0) else (exit /B 1)"]);
    return result.length >= 2 && result[1] === 0;
}

function psSingleQuoted(value)
{
    return "'" + String(value).replace(/'/g, "''") + "'";
}

function findExistingOpenThermVaneDirectory()
{
    if (thermVaneExistingTarget.length > 0) {
        return thermVaneExistingTarget;
    }

    var script =
        "$ErrorActionPreference='SilentlyContinue';" +
        "$search={" +
        "  $roots=@(Get-PSDrive -PSProvider FileSystem|ForEach-Object{$_.Root});" +
        "  foreach($root in $roots){" +
        "    $direct=@(" +
        "      (Join-Path $root 'OpenThermVane')," +
        "      (Join-Path $root 'Program Files\\OpenThermVane')," +
        "      (Join-Path $root 'Program Files (x86)\\OpenThermVane')" +
        "    );" +
        "    foreach($candidate in $direct){" +
        "      if(Test-Path -LiteralPath $candidate -PathType Container){Write-Output $candidate; return}" +
        "    }" +
        "  }" +
        "  foreach($root in $roots){" +
        "    $found=Get-ChildItem -LiteralPath $root -Directory -Filter 'OpenThermVane' -Force -Recurse -ErrorAction SilentlyContinue|" +
        "      Select-Object -First 1 -ExpandProperty FullName;" +
        "    if($found){Write-Output $found; return}" +
        "  }" +
        "};" +
        "$job=Start-Job -ScriptBlock $search;" +
        "if(Wait-Job $job -Timeout 25){Receive-Job $job|Select-Object -First 1}else{Stop-Job $job};" +
        "Remove-Job $job -Force";

    var result = installer.execute("powershell.exe", [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        script
    ]);

    thermVaneExistingTarget = firstOutputLine(result);
    return thermVaneExistingTarget;
}

function selectedTargetDirectory(widget)
{
    if (widget !== null && widget.TargetDirectoryLineEdit !== undefined) {
        return normalizePath(widget.TargetDirectoryLineEdit.text);
    }
    return normalizePath(installer.value("TargetDir"));
}

function setTargetDirectory(widget, targetDir)
{
    var normalized = normalizePath(targetDir);
    if (normalized.length === 0) {
        return;
    }

    installer.setValue("TargetDir", normalized);
    if (widget !== null && widget.TargetDirectoryLineEdit !== undefined) {
        widget.TargetDirectoryLineEdit.setText(installer.toNativeSeparators(normalized));
    }
}

function removeExistingTargetDirectory(targetDir)
{
    var target = normalizePath(targetDir);
    if (target.length < 8 || /^[A-Za-z]:\/?$/.test(target)) {
        QMessageBox.critical("ThermVane.InvalidOverwriteTarget",
            "ThermVane Installer",
            "Der ausgewählte Zielordner ist kein gültiger Installationsordner.",
            QMessageBox.Ok);
        return false;
    }

    if (!directoryExists(target)) {
        return true;
    }

    if (!installer.hasAdminRights() && !installer.gainAdminRights()) {
        QMessageBox.critical("ThermVane.AdminRequiredForOverwrite",
            "ThermVane Installer",
            "Zum Ersetzen der vorhandenen OpenThermVane-Installation werden Administratorrechte benötigt.",
            QMessageBox.Ok);
        return false;
    }

    var nativeTarget = installer.toNativeSeparators(target);
    var script =
        "$ErrorActionPreference='SilentlyContinue';" +
        "$target=" + psSingleQuoted(nativeTarget) + ";" +
        "$names=@('ThermVane','OpenThermVane','ThermVaneMaintenanceTool');" +
        "foreach($name in $names){Get-Process -Name $name -ErrorAction SilentlyContinue|Stop-Process -Force -ErrorAction SilentlyContinue};" +
        "$markers=@(" +
        "'components.xml','components.xml.sha1','components.xml.sha256','maintenance.dat','network.xml'," +
        "'ThermVaneMaintenanceTool.exe','ThermVaneMaintenanceTool.dat','ThermVaneMaintenanceTool.ini','InstallationLog.txt'" +
        ");" +
        "foreach($marker in $markers){Remove-Item -LiteralPath (Join-Path $target $marker) -Recurse -Force -ErrorAction SilentlyContinue};" +
        "Remove-Item -LiteralPath $target -Recurse -Force -ErrorAction SilentlyContinue;" +
        "if(Test-Path -LiteralPath $target -PathType Container){" +
        "  Get-ChildItem -LiteralPath $target -Force -ErrorAction SilentlyContinue|Remove-Item -Recurse -Force -ErrorAction SilentlyContinue;" +
        "  foreach($marker in $markers){Remove-Item -LiteralPath (Join-Path $target $marker) -Recurse -Force -ErrorAction SilentlyContinue}" +
        "}else{" +
        "  New-Item -ItemType Directory -Force -Path $target|Out-Null" +
        "};" +
        "$left=Get-ChildItem -LiteralPath $target -Force -ErrorAction SilentlyContinue|Select-Object -First 1;" +
        "$markerLeft=$false;" +
        "foreach($marker in $markers){if(Test-Path -LiteralPath (Join-Path $target $marker)){$markerLeft=$true}}" +
        "if($markerLeft){exit 2};" +
        "exit 0";

    var result = installer.execute("powershell.exe", [
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-Command",
        script
    ]);
    if (result.length < 2 || result[1] !== 0) {
        QMessageBox.critical("ThermVane.OverwriteFailed",
            "ThermVane Installer",
            "Der vorhandene OpenThermVane-Ordner konnte nicht ersetzt werden:\n" + nativeTarget,
            QMessageBox.Ok);
        return false;
    }

    thermVaneTargetPrepared[target] = true;
    return true;
}

function targetContainsInstallerMetadata(targetDir)
{
    var target = normalizePath(targetDir);
    var markers = [
        "components.xml",
        "components.xml.sha1",
        "components.xml.sha256",
        "maintenance.dat",
        "network.xml",
        "ThermVaneMaintenanceTool.exe",
        "ThermVaneMaintenanceTool.dat",
        "ThermVaneMaintenanceTool.ini",
        "maintenancetool.exe",
        "maintenancetool.dat",
        "maintenancetool.ini"
    ];

    for (var i = 0; i < markers.length; ++i) {
        if (fileExists(target + "/" + markers[i])) {
            return true;
        }
    }

    return false;
}

function prepareSelectedTarget(widget)
{
    var target = selectedTargetDirectory(widget);
    if (target.length === 0 || thermVaneTargetPrepared[target]) {
        return;
    }

    if (directoryExists(target)) {
        removeExistingTargetDirectory(target);
    }

    if (targetContainsInstallerMetadata(target)) {
        QMessageBox.critical("ThermVane.OverwriteStillBlocked",
            "ThermVane Installer",
            "Die bestehende Installation konnte nicht vollständig für das Überschreiben vorbereitet werden:\n"
                + installer.toNativeSeparators(target)
                + "\n\nBitte ThermVane und den MaintenanceTool-Prozess schließen und den Installer erneut als Administrator starten.",
            QMessageBox.Ok);
    }
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    var widget = gui.currentPageWidget();
    var existingTarget = findExistingOpenThermVaneDirectory();
    if (existingTarget.length > 0) {
        setTargetDirectory(widget, existingTarget);
        prepareSelectedTarget(widget);
    }

    if (widget !== null && widget.TargetDirectoryLineEdit !== undefined) {
        widget.TargetDirectoryLineEdit.editingFinished.connect(function() {
            prepareSelectedTarget(widget);
        });
    }
}

Controller.prototype.ReadyForInstallationPageCallback = function()
{
    prepareSelectedTarget(null);
}
