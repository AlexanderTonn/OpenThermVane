# OpenThermVane

OpenThermVane is a Qt 6/QML foundation for cross-platform fan and thermal control.

The first scaffold focuses on architecture:

- compact QML UI for dashboard, fans, sensors, and settings
- C++ core for fan curves, fan control, sensors, and managers
- capability-based hardware abstraction
- mock backend for development without hardware access
- prepared backend folders for Linux, Windows, and macOS
- Qt translation files for German and English
- service boundary prepared for a later privileged fan-control daemon

## Build

```sh
cmake -S . -B build
cmake --build build
./build/ThermVane
```

Qt 6.5 or newer is required.

## Architecture

QML talks to `QAbstractListModel` instances and application managers. Hardware details stay behind `IHardwareBackend`.

```text
QML
  -> FanModel / SensorModel / FanCurveModel
  -> FanManager / SensorManager / FanController
  -> IHardwareBackend
  -> Linux / Windows / macOS / mock backends
```

The GUI currently uses the mock backend. Real backends can be added without changing QML.
