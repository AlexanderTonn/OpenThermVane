# OpenThermVane

<p align="center">
  <a href="https://github.com/AlexanderTonn/OpenThermVane/actions/workflows/installer-builds.yml">
    <img src="https://github.com/AlexanderTonn/OpenThermVane/actions/workflows/installer-builds.yml/badge.svg?branch=main" alt="Installer Builds">
  </a>
  <img src="https://img.shields.io/badge/version-1.1.0-55d6be" alt="Version 1.1.0">
  <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-5865f2" alt="Platforms">
  <img src="https://img.shields.io/badge/Qt-6.5%2B-41cd52?logo=qt&logoColor=white" alt="Qt 6.5 or newer">
  <img src="https://img.shields.io/badge/CMake-3.24%2B-064f8c?logo=cmake&logoColor=white" alt="CMake 3.24 or newer">
  <img src="https://img.shields.io/github/last-commit/AlexanderTonn/OpenThermVane" alt="Last commit">
</p>

OpenThermVane is a Qt 6/QML desktop app for temperature monitoring and fan control.

## Features

- Apple Silicon SMC temperature sensors, CPU/GPU averages and MacBook fan RPM
- Per-fan sensor selection and temperature-based curves
- Manual, curve-based Auto and firmware System Auto control
- Live fan position on the curve, `.otvc` curve import/export and daily logging
- German/English UI, startup splash and automatic fan restore on exit/sleep

## Screenshots

<img src="assets/screenshots/dashboard.png" alt="Dashboard" width="600" >


<img src="assets/screenshots/Fans.png" alt="Fans and curve editor" width="600" >

## Installers

GitHub Actions builds the current installers for `main`:

- macOS: native `.dmg`
- Windows: Qt Installer Framework `.exe`
- Linux: Qt Installer Framework installer

See the [latest workflow run](https://github.com/AlexanderTonn/OpenThermVane/actions/workflows/installer-builds.yml).

## ⚠️ Unsigned builds

> [!WARNING]
> Release artifacts are currently **not code-signed or notarized**. Run them only when the download originates from a trusted repository or release.
>
> Do not disable Gatekeeper, Defender or other system-wide security controls globally.

### macOS

> [!CAUTION]
> macOS may block both the unsigned package and the installed app. The steps below explicitly approve this trusted copy.

1. Open the `.dmg` and run `ThermVane.pkg`.
2. If macOS blocks the package, Control-click it, choose **Open**, then confirm.
3. After installation, Control-click `ThermVane.app`, choose **Open**, then confirm **Open**.
4. If Gatekeeper still blocks it: **System Settings → Privacy & Security → Open Anyway**.

Do not disable Gatekeeper globally. The installer registers the privileged fan helper; macOS may ask once for administrator approval.

### Windows

> [!CAUTION]
> Windows SmartScreen may warn because the installer has no publisher signature.

1. Start the installer.
2. If SmartScreen appears, select **More info → Run anyway** after verifying the download source.
3. If Windows marks the file as blocked: right-click it, open **Properties**, select **Unblock**, then apply.

The installer may require administrator rights for fan-control components.

### Linux

Run the downloaded installer and grant administrator privileges when requested.

## Build from source

Requirements: Qt 6.5+ and CMake 3.24+.

```sh
cmake -S . -B build
cmake --build build
./build/ThermVane
```

For manual fan control on macOS during development:

```sh
./scripts/macos/build-and-install-fan-helper.sh
```

Remove the helper with:

```sh
./scripts/macos/uninstall-fan-helper.sh
```
