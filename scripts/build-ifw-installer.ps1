param(
    [string]$BuildDir = "build\Desktop_Qt_6_12_0_llvm_mingw_64_bit_Release",
    [ValidateSet("Release")]
    [string]$Configuration = "Release",
    [string]$IfwRoot = "",
    [string]$NbfcInstaller = "",
    [string]$OutputDir = "build/ifw",
    [string]$InstallerName = "ThermVaneInstaller.exe",
    [string]$QtRoot = "D:\QT\6.12.0\llvm-mingw_64"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPath = Join-Path $RepoRoot $BuildDir
$OutputPath = Join-Path $RepoRoot $OutputDir
$InstallRoot = Join-Path $OutputPath "install-root"
$PackagesRoot = Join-Path $OutputPath "packages"
$QtBin = Join-Path $QtRoot "bin"

Write-Host "[ThermVane IFW] Build directory: $BuildPath"
Write-Host "[ThermVane IFW] Configuration: $Configuration"

if (-not $IfwRoot) {
    $binaryCreator = Get-Command binarycreator.exe -ErrorAction SilentlyContinue
    if (-not $binaryCreator) {
        throw "binarycreator.exe not found. Pass -IfwRoot <QtIFW root> or add Qt Installer Framework bin directory to PATH."
    }
    $BinaryCreatorPath = $binaryCreator.Source
} else {
    $BinaryCreatorPath = Join-Path $IfwRoot "bin/binarycreator.exe"
}
if (-not (Test-Path $BinaryCreatorPath)) {
    throw "binarycreator.exe not found at '$BinaryCreatorPath'."
}
Write-Host "[ThermVane IFW] binarycreator: $BinaryCreatorPath"

if (-not (Test-Path $BuildPath)) {
    Write-Host "[ThermVane IFW] Creating Release build directory..."
    New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
    cmake -S $RepoRoot -B $BuildPath -DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_PREFIX_PATH=$QtRoot
}

Write-Host "[ThermVane IFW] Building ThermVane..."
cmake --build $BuildPath --config $Configuration
Write-Host "[ThermVane IFW] Installing ThermVane to staging directory..."
cmake --install $BuildPath --config $Configuration --prefix $InstallRoot

$windeployqtPath = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path $windeployqtPath)) {
    $windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($windeployqt) {
        $windeployqtPath = $windeployqt.Source
    }
}
if (-not (Test-Path $windeployqtPath)) {
    throw "windeployqt.exe not found. Pass -QtRoot <Qt kit root> or add Qt bin directory to PATH."
}
Write-Host "[ThermVane IFW] windeployqt: $windeployqtPath"

$thermVaneExe = Join-Path $InstallRoot "bin/ThermVane.exe"
Write-Host "[ThermVane IFW] Deploying Qt runtime. Last visible line is often qt_zh_TW.qm; next step may take a while..."
& $windeployqtPath $thermVaneExe --qmldir (Join-Path $RepoRoot "qml") --compiler-runtime
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}
Write-Host "[ThermVane IFW] Qt runtime deployment finished."

$runtimeDlls = @(
    "libc++.dll",
    "libunwind.dll"
)
foreach ($dll in $runtimeDlls) {
    $source = Join-Path $QtBin $dll
    if (Test-Path $source) {
        Write-Host "[ThermVane IFW] Copying runtime DLL: $dll"
        Copy-Item $source (Join-Path (Split-Path $thermVaneExe) $dll) -Force
    }
}

if (Test-Path $PackagesRoot) {
    Write-Host "[ThermVane IFW] Removing old IFW package staging..."
    Remove-Item -Recurse -Force $PackagesRoot
}
Write-Host "[ThermVane IFW] Copying IFW package metadata..."
Copy-Item -Recurse (Join-Path $RepoRoot "installer/packages") $PackagesRoot

$appData = Join-Path $PackagesRoot "org.openthermvane.app/data"
New-Item -ItemType Directory -Force -Path $appData | Out-Null
Write-Host "[ThermVane IFW] Copying deployed application into IFW package..."
Copy-Item -Recurse (Join-Path $InstallRoot "*") $appData

$nbfcData = Join-Path $PackagesRoot "org.openthermvane.nbfc/data/nbfc"
New-Item -ItemType Directory -Force -Path $nbfcData | Out-Null
if ($NbfcInstaller) {
    $payload = Resolve-Path $NbfcInstaller
    $extension = [System.IO.Path]::GetExtension($payload).ToLowerInvariant()
    if ($extension -eq ".msi") {
        Write-Host "[ThermVane IFW] Adding NBFC MSI payload..."
        Copy-Item $payload (Join-Path $nbfcData "nbfc-installer.msi")
    } elseif ($extension -eq ".exe") {
        Write-Host "[ThermVane IFW] Adding NBFC EXE payload..."
        Copy-Item $payload (Join-Path $nbfcData "nbfc-installer.exe")
    } else {
        throw "NBFC installer must be .msi or .exe"
    }
} else {
    Write-Warning "No -NbfcInstaller provided. NBFC component will install no payload."
}

New-Item -ItemType Directory -Force -Path $OutputPath | Out-Null
$InstallerPath = Join-Path $OutputPath $InstallerName
if (Test-Path $InstallerPath) {
    Write-Host "[ThermVane IFW] Removing old installer: $InstallerPath"
    Remove-Item -Force $InstallerPath
}
Write-Host "[ThermVane IFW] Creating offline installer. This step can be silent for several minutes..."
& $BinaryCreatorPath `
    --offline-only `
    -c (Join-Path $RepoRoot "installer/config/config.xml") `
    -p $PackagesRoot `
    $InstallerPath
if ($LASTEXITCODE -ne 0) {
    throw "binarycreator failed with exit code $LASTEXITCODE"
}
Write-Host "[ThermVane IFW] Installer created: $InstallerPath"
