# Minas Documentation

## Current architecture

Minas is an embedded research prototype with two firmware targets:

- **Controller Unit:** ESP32-WROVER/ESP32 Classic. It connects to the PS5 DualSense using Bluetooth Classic, reads controller inputs, stores labeled trial data on an external SPI microSD card, and sends authorized commands over ESP-NOW when the Vehicle Unit MAC is configured.
- **Vehicle Unit:** ESP32-S3-DevKitC-1 N16R8 mounted on the car. It validates commands from the configured Controller Unit MAC, drives the steering servo and ESC, sends telemetry, and enters neutral on timeout or denial.

The Controller Unit can run without the Vehicle Unit MAC while it is unavailable. In that local test mode, it can connect to the DualSense and collect SD data, but it does not send ESP-NOW commands.

## Data collection

The binary research labels are `owner` and `nonowner`. The label is manually selected as ground truth for each trial and is not proof of identity. Pressing Circle toggles the label and opens a new CSV file. A trial should contain one driver, one label and one continuous driving segment.

The current firmware is configured for collection bypass:

```cpp
#define CONTROLLER_OPERATION_MODE 1
```

No trained ML model is included. The classifier function is a placeholder. Modes 2 and 3 are reserved for future shadow evaluation and enforcement after a real model has been integrated and validated.

Before training, convert rows into time windows and split train, validation and test data by driver and session rather than by random rows.

## Hardware and configuration

The Controller Unit requires an original ESP32/WROVER-class board with Bluetooth Classic, a DualSense and an external SPI microSD module. The Vehicle Unit requires the ESP32-S3-DevKitC-1 N16R8, a steering servo, an ESC and optionally an HC-SR04.

Configure the DualSense MAC in `ControllerUnit/include/Config.h`. Configure the Vehicle Unit STA MAC in `vehicleUnitAddress` before enabling the full wireless link. While the S3 is unavailable, the address may remain all zeros for local PS5 + SD testing. Configure the Controller Unit STA MAC in `VehicleUnit/include/Config.h` before operating the vehicle.

The default SD wiring is:

| SD signal | Controller Unit GPIO |
| --- | --- |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| CS | 5 |

The default Vehicle Unit pins are:

| Function | GPIO |
| --- | --- |
| Steering servo | 25 |
| ESC | 26 |
| Sonar trigger | 22 |
| Sonar echo | 23 |

Verify the actual board wiring. Do not connect a 5 V HC-SR04 Echo signal directly to an ESP32 GPIO; use a divider or level shifter.

## Build

```bash
cd ControllerUnit
pio run

cd ../VehicleUnit
pio run
```

The Controller Unit uses the original ESP32 target for Bluetooth Classic. The Vehicle Unit uses the ESP32-S3 target with 16 MB flash and OPI PSRAM settings defined in its `platformio.ini`.

## Bench test

Before connecting the motor, lift the wheels or disconnect the motor mechanically. Confirm that the Vehicle Unit starts with centered steering and ESC neutral. For local testing without the S3, confirm that the PS5 connects and the Controller Unit creates a trial file on the SD card. After configuring both MAC addresses, confirm that the Vehicle Unit receives valid commands and returns to neutral when the Controller Unit or PS5 is disconnected.

## Safety and security limitations

These are research-prototype safeguards, not certified safety controls. CRC32 detects accidental corruption but is not authentication. The current system does not provide authenticated encryption, secure provisioning, complete replay protection or certified vehicle safety. Battery monitoring and a Battery Guardian are not implemented.

Bench-test with the wheels lifted or the motor disconnected before applying battery power.

## Legacy files

`shared/MRP.cpp`, `shared/MRP.h` and `Docs/MRP_Specification.md` describe an older protocol prototype. The current command path uses `shared/MinasProtocol.h`, `AuthorizedVehicleCommand` and `VehicleTelemetry`. The legacy files are retained for historical reference and should not be treated as the current transport specification.
