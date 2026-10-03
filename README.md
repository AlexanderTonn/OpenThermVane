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

After that, ThermVane talks to the running helper over a local Unix socket. If launchd is not currently serving the socket, ThermVane can fall back to the installed privileged helper binary. It does not ask for a password on every fan change.

To remove it:

```sh
./scripts/macos/uninstall-fan-helper.sh
```

The helper socket is `/tmp/thermvane-fan-helper.sock`. The installed helper path is `/Library/PrivilegedHelperTools/ThermVaneFanHelper`. The helper is restarted by launchd and logs to `/var/log/thermvane-fan-helper.log`.

## Architecture

QML talks to `QAbstractListModel` instances and application managers. Hardware details stay behind `IHardwareBackend`.

```text
QML
  -> FanModel / SensorModel / FanCurveModel
  -> FanManager / SensorManager / FanController
  -> IHardwareBackend
  -> Linux / Windows / macOS / mock backends
```
