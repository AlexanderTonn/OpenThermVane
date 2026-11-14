# OpenThermVane

OpenThermVane is a Qt 6/QML app for fan and thermal monitoring/control.

Current focus:

- QML UI for dashboard, fans, sensors, and settings
- Apple Silicon temperature sensor detection
- MacBook fan RPM display and manual control
- fan curves per fan and selected temperature sensor
- mock backend for development without hardware access
- prepared backend folders for Linux, Windows, and macOS
- Qt translation files for German and English

## Build

```sh
cmake -S . -B build
cmake --build build
./build/ThermVane
```

Qt 6.5 or newer is required.

## macOS fan helper

macOS blocks SMC fan writes from a normal GUI process. For manual fan control, install the helper once as a LaunchDaemon:

```sh
./scripts/macos/install-fan-helper.sh
```

For a local build, this also builds and installs the helper in one step:

```sh
./scripts/macos/build-and-install-fan-helper.sh
```

After that, ThermVane talks to the running helper over a local Unix socket. On an installed system, manual writes require the lease-backed helper server; if it is unavailable, ThermVane refuses an unsafe one-shot privileged write. The installed helper binary remains available for restoring system control. It does not ask for a password on every fan change.

For development-only machines without the installed helper, the unsafe direct SMC fallback can be explicitly enabled with `-DTHERMVANE_ALLOW_UNLEASED_DIRECT_SMC=ON`. Release builds leave this option disabled so a crashed application cannot leave a manual fan override behind without the helper lease.

To remove it:

```sh
./scripts/macos/uninstall-fan-helper.sh
```

Beim Windows-IFW-Build wird eine NBFC-Payload mit `-NbfcInstaller` nur akzeptiert, wenn sie authenticode-signiert ist oder zusätzlich der erwartete SHA-256-Hash über `-NbfcSha256` angegeben wird. Der Hash wird in das Paket aufgenommen und vor der Installation erneut geprüft.

The helper socket is `/tmp/thermvane-fan-helper.sock` and is restricted to the user who installed ThermVane. The installed helper path is `/Library/PrivilegedHelperTools/ThermVaneFanHelper`. The helper is restarted by launchd and logs to `/var/log/thermvane-fan-helper.log`.

## Architecture

QML talks to `QAbstractListModel` instances and application managers. Hardware details stay behind `IHardwareBackend`.

```text
QML
  -> FanModel / SensorModel / FanCurveModel
  -> FanManager / SensorManager / FanController
  -> IHardwareBackend
  -> Linux / Windows / macOS / mock backends
```
