# Minas — Embedded Driver-Recognition Research Platform

Minas is an open-source embedded research platform for collecting RC-car driving telemetry and studying whether driving behavior can help distinguish an authorized owner from non-owner drivers.

> **Current status:** Minas is a research data-collection prototype. The repository does not currently contain a trained driver-classification model, production-grade authorization security, or certified vehicle-safety controls.

## Research question

The long-term research question is whether an authorized driver's behavioral patterns can be identified reliably while telemetry is transferred between an RC vehicle and a base station. The current repository focuses on building and instrumenting the data-collection platform. Driver classification and active authorization are future research stages.

## Architecture

```text
PS5 DualSense
      │ Bluetooth Classic
      ▼
Controller Unit — ESP32/WROVER
      ├── reads steering, throttle, triggers and buttons
      ├── stores labeled trial data on microSD
      └── sends telemetry over ESP-NOW
                    │
                    ▼
Vehicle Unit — ESP32-S3
      ├── validates the configured sender and sequence
      ├── controls steering and ESC outputs
      ├── reports vehicle-side telemetry
      └── returns outputs to neutral after timeout or denial
```

The Controller Unit uses an original ESP32/WROVER-class board because the selected DualSense library requires Bluetooth Classic. The Vehicle Unit uses an ESP32-S3 and does not connect directly to the PS5 controller.

## Current data-collection workflow

A trial is manually labeled as `owner` or `nonowner`. The label is experimental ground truth; it is not cryptographic proof of the driver's identity. Pressing the configured controller button changes the active label and starts a new trial file.

Each trial contains metadata followed by telemetry rows. The intended future machine-learning pipeline should transform rows into time windows and split training, validation and test data by driver and session rather than by random rows. Random row-level splitting can leak adjacent samples from the same drive across data partitions.

## Machine-learning status

The current firmware does **not** include a trained driver-classification model. `classifyDriver()` is a placeholder interface for a future classifier. The current collection mode permits data collection for both labels. Modes reserved for shadow evaluation and enforcement are not an implemented ML system in this release.

Before any enforcement mode is considered, the project needs a real model, held-out drivers and sessions, false-accept and false-reject measurements, latency measurements, a failure policy, and a safety review.

## Minas Rolling-Key Protocol (MRP)

MRP is a research transport layer for the Controller-to-Vehicle link. The current implementation provides message structure, sequence handling, a rolling-key derivation mechanism, encrypted payload transport and encrypted acknowledgements.

MRP is **not production-grade authorization security**. The current research implementation uses AES-ECB, a fixed bootstrap seed compiled into both firmware images, and no authenticated-encryption tag. Secure provisioning, robust key lifecycle management, complete replay protection and a certified safety mechanism are not implemented.

The protocol should therefore be treated as a laboratory transport experiment and not as a security boundary for a real vehicle or safety-critical system.

## Hardware

### Controller Unit

The Controller Unit requires an ESP32/WROVER-class board with Bluetooth Classic, a PS5 DualSense controller, an external SPI microSD module and the configured wiring described in `ControllerUnit/include/Config.h`.

### Vehicle Unit

The Vehicle Unit requires an ESP32-S3-DevKitC-1-class board, a steering servo, an ESC and optionally an HC-SR04 ultrasonic sensor. Verify voltage levels, shared ground, servo range and ESC behavior before connecting a battery or motor.

The HC-SR04 Echo line must not be connected directly to an ESP32 GPIO when the sensor can output 5 V. Use an appropriate voltage divider or level shifter.

## Build

```shell
cd ControllerUnit
pio run

cd ../VehicleUnit
pio run
```

Before uploading firmware, configure the local board addresses and hardware settings. Do not commit personal board addresses or laboratory secrets unless they are explicitly dummy values.

## Safety procedure

Do not connect the motor or apply battery power during the first test. Begin with the wheels lifted or the motor mechanically disconnected. Verify neutral output, timeout behavior, invalid-sequence handling, packet-loss behavior and recovery after reboot before testing motion.

These safeguards are research-prototype safeguards, not certified vehicle-safety controls.

## Reproducibility checklist

For each collection session, record the board models, firmware revision, library revisions, wiring revision, telemetry interval, route or task, driver label, session identifier, trial number, SD-card format and relevant environmental conditions.

## Repository structure

```text
ControllerUnit/   Controller firmware
VehicleUnit/      Vehicle firmware
shared/           Shared MRP structures and implementation
Docs/             Protocol and wiring documentation
```

## Roadmap

- Add a transport test harness independent of ESP32 hardware.
- Test duplicate, delayed, reordered and dropped packets.
- Define a secure provisioning design instead of a compiled bootstrap seed.
- Replace AES-ECB with authenticated encryption in a future protocol revision.
- Add real driver-classification experiments with held-out drivers and sessions.
- Publish latency, packet-loss, false-accept and false-reject measurements.
- Document failure behavior and hardware safety assumptions.

## Local hardware configuration

The public repository builds with safe all-zero/example values and does not require personal hardware addresses. For a local hardware setup, copy the appropriate example files:

```shell
cp ControllerUnit/include/personalConfig.example.h \\
   ControllerUnit/include/personalConfig.h

cp VehicleUnit/include/personalConfig.example.h \\
   VehicleUnit/include/personalConfig.h
```

Edit the local files with your own PS5, Controller Unit and Vehicle Unit addresses. `personalConfig.h` is intentionally ignored by Git and must never be committed. The example files contain only dummy values.

## Build with PlatformIO

From the repository root:

```shell
pio run --project-dir ControllerUnit
pio run --project-dir VehicleUnit
```

The same two builds run automatically in GitHub Actions for pushes to `main` and pull requests. The public CI build uses the safe fallback configuration and does not require personal hardware addresses.

## License and contribution

This repository is an educational and research project. Contributions should describe the tested hardware, firmware revision, experimental conditions and known limitations. Established technologies such as ESP-NOW, AES, SHA-256 and CRC are used as components; Minas does not claim to have invented them.
