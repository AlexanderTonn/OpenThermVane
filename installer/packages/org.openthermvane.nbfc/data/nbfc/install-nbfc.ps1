param(
    [Parameter(Mandatory = $true)]
    [string]$TargetDir
)

$ErrorActionPreference = 'Stop'

$target = [System.IO.Path]::GetFullPath($TargetDir)
$payloadDir = Join-Path $target 'nbfc'
$installerExe = Join-Path $payloadDir 'nbfc-installer.exe'
$installerMsi = Join-Path $payloadDir 'nbfc-installer.msi'
$log = Join-Path $target 'nbfc-install.log'

function Write-InstallLog {
    param([string]$Message)
    Add-Content -LiteralPath $log -Value ((Get-Date -Format s) + ' ' + $Message)
}

function Test-NbfcInstalled {
    if (Get-Service -Name 'NoteBookFanControlService' -ErrorAction SilentlyContinue) {
        return $true
    }

    $roots = @(
        $env:ProgramFiles,
        ${env:ProgramFiles(x86)},
        $env:LOCALAPPDATA
    ) | Where-Object { $_ }

    foreach ($root in $roots) {
        foreach ($relative in @(
            'NoteBook FanControl\nbfc.exe',
            'NoteBookFanControl\nbfc.exe',
            'NBFC\nbfc.exe',
            'nbfc\nbfc.exe'
        )) {
            if (Test-Path -LiteralPath (Join-Path $root $relative)) {
                return $true
            }
        }
    }

    return $false
}

New-Item -ItemType Directory -Force -Path $target | Out-Null
Write-InstallLog "Starting NBFC installation in $target"

if (Test-NbfcInstalled) {
    Write-InstallLog 'NBFC is already installed'
} elseif (Test-Path -LiteralPath $installerMsi) {
    Write-InstallLog 'Installing NBFC MSI payload'
    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', $installerMsi, '/qn', '/norestart') -Wait -PassThru
    Write-InstallLog ('NBFC MSI exit code: ' + $process.ExitCode)
    if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
        throw ('NBFC MSI installation failed: ' + $process.ExitCode)
    }
} elseif (Test-Path -LiteralPath $installerExe) {
    Write-InstallLog 'Installing NBFC EXE payload'
    $silentArguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART')
    $process = Start-Process -FilePath $installerExe -ArgumentList $silentArguments -Wait -PassThru
    Write-InstallLog ('NBFC EXE exit code: ' + $process.ExitCode)
    if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
        throw ('NBFC EXE installation failed: ' + $process.ExitCode)
    }
} else {
    throw 'NBFC installer payload not found'
}

if (-not (Test-NbfcInstalled)) {
    throw 'NBFC installation finished but nbfc.exe/service was not found'
}

if (Get-Service -Name 'NoteBookFanControlService' -ErrorAction SilentlyContinue) {
    Start-Service -Name 'NoteBookFanControlService' -ErrorAction SilentlyContinue
}

Remove-Item -LiteralPath $payloadDir -Recurse -Force -ErrorAction SilentlyContinue
Write-InstallLog 'NBFC installation completed'
