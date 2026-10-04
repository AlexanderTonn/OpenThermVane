# Packaging

ThermVane uses Qt Installer Framework (QtIFW) for offline installers.

## Submodules

NBFC is included as a Git submodule:

```bash
git submodule update --init --recursive
```

## macOS: build native DMG installer

Build a macOS `.dmg` with a native `.pkg` installer:

```bash
./scripts/package-macos-dmg.sh
```

The script uses these defaults:

- Qt: `/Volumes/AlexMacSSD/Qt/6.12.0/macos`
- Build directory: `build/Qt_6_12_0_for_macOS_Release`

Override them when needed:

```bash
QT_ROOT="/path/to/Qt/6.x/macos" \
BUILD_DIR="build/macos-release" \
./scripts/package-macos-dmg.sh
```

The output is written to `<build-dir>/macos-dmg/ThermVaneInstaller.dmg`.

The DMG contains `ThermVane.pkg`. The package installs `ThermVane.app` directly to `/Applications` and runs a root `postinstall` script to install `ThermVaneFanHelper` into `/Library/PrivilegedHelperTools/ThermVaneFanHelper` and register `/Library/LaunchDaemons/com.openthermvane.fanhelper.plist`.

The old QtIFW macOS installer is no longer used for release builds because it installs into an application folder and can show QtIFW maintenance package warnings on macOS. Windows and Linux still use QtIFW.

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

The workflow `.github/workflows/installer-builds.yml` builds a native DMG installer for macOS and QtIFW installers for Windows and Linux. It runs on pull requests, pushes to `main`, version tags and manual `workflow_dispatch` runs.

The macOS job installs Qt with `aqtinstall`, builds the `package_macos_dmg` target and uploads the generated DMG. The Windows and Linux jobs install Qt plus Qt Installer Framework, build `package_ifw` and upload their installers as artifacts:

- macOS: `ThermVaneInstaller.dmg`
- Windows: `ThermVaneInstaller.exe`
- Linux: `ThermVaneInstaller`

The workflow uses these shared variables at the top of the YAML file:

- `QT_VERSION`: Qt version used on all runners.
- `IFW_TOOL_ID`: Qt Installer Framework package id used by `aqtinstall`.

Windows CI omits the optional NBFC component unless `THERMVANE_NBFC_INSTALLER` points to a `.msi` or `.exe` payload during packaging. macOS CI builds a DMG containing a native pkg that installs the app and fan helper. Linux CI copies the Qt runtime libraries, Qt plugins and QML imports from the installed Qt kit into the IFW package.

## CMake QtIFW target

The same package step is available from CMake:

```powershell
cmake -S . -B <build-dir> `
  -DTHERMVANE_IFW_BINARYCREATOR="D:\QT\Tools\QtInstallerFramework\4.11\bin\binarycreator.exe" `
  -DTHERMVANE_QT_ROOT="D:\QT\6.12.0\llvm-mingw_64" `
  -DTHERMVANE_NBFC_INSTALLER="C:\path\to\NBFC-installer.exe"

cmake --build <build-dir> --target package_ifw --config Release
```

On macOS, use the `package_macos_dmg` target instead of `package_ifw`. Set `THERMVANE_QT_ROOT` to the Qt macOS kit root.

The output is written to `<build-dir>/macos-dmg/ThermVaneInstaller.dmg` on macOS, `<build-dir>/ifw/ThermVaneInstaller.exe` on Windows and `<build-dir>/ifw/ThermVaneInstaller` on Linux.
