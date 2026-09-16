# Packaging

ThermVane uses Qt Installer Framework (QtIFW) for offline installers.

## Submodules

NBFC is included as a Git submodule:

```bash
git submodule update --init --recursive
```

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

## CMake QtIFW target

The same package step is available from CMake:

```powershell
cmake -S . -B <build-dir> `
  -DTHERMVANE_IFW_BINARYCREATOR="D:\QT\Tools\QtInstallerFramework\4.11\bin\binarycreator.exe" `
  -DTHERMVANE_QT_ROOT="D:\QT\6.12.0\llvm-mingw_64" `
  -DTHERMVANE_NBFC_INSTALLER="C:\path\to\NBFC-installer.exe"

cmake --build <build-dir> --target package_ifw --config Release
```

The output is written to `<build-dir>/ifw/ThermVaneInstaller`.
