# Packaging

ThermVane uses Qt Installer Framework (QtIFW) for offline installers.

## Submodules

NBFC is included as a Git submodule:

```bash
git submodule update --init --recursive
```

## macOS: build ThermVane QtIFW installer

Build a macOS offline installer with:

```bash
./scripts/package-ifw-installer-macos.sh
```

The script uses these defaults:

- Qt: `/Volumes/AlexMacSSD/Qt/6.12.0/macos`
- Qt Installer Framework: `/Volumes/AlexMacSSD/Qt/Tools/QtInstallerFramework/4.11`
- Build directory: `build/Qt_6_12_0_for_macOS_Release`

Override them when needed:

```bash
QT_ROOT="/path/to/Qt/6.x/macos" \
IFW_ROOT="/path/to/QtInstallerFramework/4.x" \
BUILD_DIR="build/macos-release" \
./scripts/package-ifw-installer-macos.sh
```

The output is written to `<build-dir>/ifw/ThermVaneInstaller.app`.

The macOS installer contains:

- `ThermVane.app`, deployed with `macdeployqt`.
- `ThermVaneFanHelper`.
- macOS helper install/uninstall scripts.

During installation, the app component runs the helper installation elevated once. It installs the helper to `/Library/PrivilegedHelperTools/ThermVaneFanHelper` and registers the LaunchDaemon `/Library/LaunchDaemons/com.openthermvane.fanhelper.plist`. During uninstall, the LaunchDaemon and helper are removed.

## Linux: build NBFC from source

NBFC can be built from the submodule with Mono/xbuild:

```bash
cmake --build <build-dir> --target nbfc_linux_build
```

This runs `external/nbfc/build.sh` and leaves NBFC's Linux output in the NBFC tree.

## Windows: build NBFC

When Visual Studio/MSBuild and NBFC's Windows build requirements are available:

```powershell
cmake --build <build-dir> --target nbfc_windows_build
```

This runs `external/nbfc/build.ps1`. NBFC's own build produces its Windows artifacts through the upstream solution/WiX projects.

## Windows: build ThermVane QtIFW installer

Build ThermVane, provide a NBFC installer payload, then run:

```powershell
.\scripts\build-ifw-installer.ps1 `
  -QtRoot "D:\QT\6.12.0\llvm-mingw_64" `
  -IfwRoot "D:\QT\Tools\QtInstallerFramework\4.11" `
  -NbfcInstaller "C:\path\to\NBFC-installer.exe"
```

`-NbfcInstaller` accepts `.exe` or `.msi`.
`-BuildDir` defaults to the Release build directory:
`build\Desktop_Qt_6_12_0_llvm_mingw_64_bit_Release`.

The QtIFW package contains:

- `org.openthermvane.app`: ThermVane application.
- `org.openthermvane.nbfc`: NBFC installer payload.

During installation, the NBFC component runs the payload elevated and silently:

- `.msi`: `msiexec /i ... /qn /norestart`
- `.exe`: `... /quiet /norestart`

## GitHub Actions: installer builds

The workflow `.github/workflows/installer-builds.yml` builds QtIFW installers for macOS, Windows and Linux. It runs on pull requests, pushes to `main`, version tags and manual `workflow_dispatch` runs.

Each job installs Qt and Qt Installer Framework with `aqtinstall`, configures CMake in Release mode, builds the `package_ifw` target and uploads the generated installer as an artifact:

- macOS: `ThermVaneInstaller.app`
- Windows: `ThermVaneInstaller.exe`
- Linux: `ThermVaneInstaller`

The workflow uses these shared variables at the top of the YAML file:

- `QT_VERSION`: Qt version used on all runners.
- `IFW_TOOL_ID`: Qt Installer Framework package id used by `aqtinstall`.

Windows CI omits the optional NBFC component unless `THERMVANE_NBFC_INSTALLER` points to a `.msi` or `.exe` payload during packaging. macOS CI includes the fan helper and helper install scripts in the installer. Linux CI copies the Qt runtime libraries, Qt plugins and QML imports from the installed Qt kit into the IFW package.

## CMake QtIFW target

The same package step is available from CMake:

```powershell
cmake -S . -B <build-dir> `
  -DTHERMVANE_IFW_BINARYCREATOR="D:\QT\Tools\QtInstallerFramework\4.11\bin\binarycreator.exe" `
  -DTHERMVANE_QT_ROOT="D:\QT\6.12.0\llvm-mingw_64" `
  -DTHERMVANE_NBFC_INSTALLER="C:\path\to\NBFC-installer.exe"

cmake --build <build-dir> --target package_ifw --config Release
```

On macOS, set `THERMVANE_QT_ROOT` to the Qt macOS kit root and `THERMVANE_IFW_BINARYCREATOR` to the `binarycreator` executable.

The output is written to `<build-dir>/ifw/ThermVaneInstaller.app` on macOS, `<build-dir>/ifw/ThermVaneInstaller.exe` on Windows and `<build-dir>/ifw/ThermVaneInstaller` on Linux.
