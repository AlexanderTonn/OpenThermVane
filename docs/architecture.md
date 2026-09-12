# ThermVane Architecture

## Boundaries

QML must not know OS APIs or hardware paths. It receives normalized fans, sensors, capabilities, and curve points.

## Core

- `FanCurve`: sorted points plus linear interpolation.
- `FanController`: safety policy, emergency temperature, minimum speed, mode decisions.
- `FanManager`: scanned fans and speed commands.
- `SensorManager`: scanned temperature sensors.

## Hardware

`IHardwareBackend` is the only interface between core logic and device access.

Backends report capabilities per fan:

- RPM reading
- manual control
- firmware control
- zero RPM
- PWM control

## Service

The `service` folder contains the future split between GUI and privileged hardware service. The planned transport is `QLocalSocket`, using local domain sockets on Unix-like systems and named pipes on Windows.
