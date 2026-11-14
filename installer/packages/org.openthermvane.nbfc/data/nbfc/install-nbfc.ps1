param(
    [Parameter(Mandatory = $true)]
    [string]$TargetDir
)

$ErrorActionPreference = 'Stop'

$target = [System.IO.Path]::GetFullPath($TargetDir)
$payloadDir = Join-Path $target 'nbfc'
$installerExe = Join-Path $payloadDir 'nbfc-installer.exe'
$installerMsi = Join-Path $payloadDir 'nbfc-installer.msi'
$hashFile = Join-Path $payloadDir 'nbfc-installer.sha256'
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
        ${env:ProgramFiles(x86)}
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

function Test-PayloadIntegrity {
    param([string]$Payload)

    if (Test-Path -LiteralPath $hashFile -PathType Leaf) {
        $expected = (Get-Content -LiteralPath $hashFile -Raw).Trim().ToUpperInvariant()
        if ($expected -notmatch '^[0-9A-F]{64}$') {
            throw 'NBFC payload hash file is invalid'
        }
        $actual = (Get-FileHash -LiteralPath $Payload -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($actual -ne $expected) {
            throw 'NBFC payload integrity check failed'
        }
        return
    }

    $signature = Get-AuthenticodeSignature -FilePath $Payload
    if ($signature.Status -ne 'Valid') {
        throw 'NBFC payload has no valid Authenticode signature or hash manifest'
    }
}

New-Item -ItemType Directory -Force -Path $target | Out-Null
Write-InstallLog "Starting NBFC installation in $target"

if (Test-NbfcInstalled) {
    Write-InstallLog 'NBFC is already installed'
} elseif (Test-Path -LiteralPath $installerMsi) {
    Write-InstallLog 'Installing NBFC MSI payload'
    Test-PayloadIntegrity $installerMsi
    $process = Start-Process -FilePath 'msiexec.exe' -ArgumentList @('/i', $installerMsi, '/qn', '/norestart') -Wait -PassThru
    Write-InstallLog ('NBFC MSI exit code: ' + $process.ExitCode)
    if ($process.ExitCode -ne 0 -and $process.ExitCode -ne 3010) {
        throw ('NBFC MSI installation failed: ' + $process.ExitCode)
    }
} elseif (Test-Path -LiteralPath $installerExe) {
    Write-InstallLog 'Installing NBFC EXE payload'
    Test-PayloadIntegrity $installerExe
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
