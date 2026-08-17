# Minas: Owner-Recognition Research Platform

Minas is an embedded research platform for collecting RC-car driving telemetry and studying whether the driving behavior of an authorized owner can be distinguished from that of non-owner drivers.

> **Current status:** Minas is a research data-collection prototype. It does not yet contain a trained driver-classification model, production-grade authorization security, or certified vehicle-safety controls.

## Architecture

```
PS5 DualSense
      │ Bluetooth Classic
      ▼
Controller Unit — ESP32-WROVER
      ├── connects to the PS5 controller
      ├── reads steering, throttle, triggers and selected buttons
      ├── stores trial data on an external SPI microSD card
      ├── provides the future ML decision gate
      └── sends a command over ESP-NOW
                    │
                    ▼
Vehicle Unit — ESP32-S3 mounted on the car
      ├── validates the command and sender MAC
      ├── drives the steering servo and ESC
      ├── reports vehicle telemetry
      └── returns the servo and ESC to neutral on timeout or denial
```

The **Controller Unit** uses an original ESP32/WROVER-class board because the selected DualSense library requires Bluetooth Classic. The **Vehicle Unit** uses an ESP32-S3 and does not connect to the PS5 controller.

## Data-collection workflow

The target research label is binary: `owner` versus `nonowner`. The label is manually assigned as ground truth for each trial. It is not cryptographic proof of the driver’s identity.

The Controller Unit creates a separate CSV trial file for each label interval. Pressing Circle toggles the manually assigned label and opens a new file. One trial should contain one driver, one fixed label and one continuous experimental segment.

Example files are:

```
/trial_1_owner.csv
/trial_2_nonowner.csv
```

Each file contains metadata followed by rows with the following fields:

```
trial_number,input_sequence,timestamp_ms,steering,throttle,l2,r2,buttons_mask,owner_label,ml_decision,confidence_permille,allow_motion,vehicle_sonar_cm,vehicle_failsafe
```

The data should be transformed into time windows before machine-learning training. Train, validation and test splits must be made by driver and session rather than by random individual rows, otherwise adjacent samples from the same drive can leak between the splits.

## Machine-learning status

A trained model is not included in the current firmware. The function `classifyDriver()` is a placeholder for a future classifier. In the current firmware, the manually assigned label is ground truth for collection; it must not be described as an ML prediction.

The current controller mode is:

| Mode | Meaning |
| --- | --- |
| `1` | Collection bypass. Used to collect labeled data; no trained model is active. |
| `2` | Reserved for future shadow evaluation. |
| `3` | Reserved for future enforcement after a real model has been integrated and validated. |

Modes `2` and `3` are not a working ML implementation in this release. Before enforcement is enabled, the placeholder must be replaced with a real model and evaluated on drivers and sessions that were not used for training.

## Hardware requirements

The Controller Unit requires an original ESP32/WROVER-class board with Bluetooth Classic, an external SPI microSD module and a PS5 DualSense controller. The Vehicle Unit requires an ESP32-S3-DevKitC-1 N16R8, a steering servo, an ESC and optionally an HC-SR04 ultrasonic sensor.

The HC-SR04 Echo signal must not be connected directly to an ESP32 GPIO if the sensor can output 5 V. Use a voltage divider or level shifter. The ESP32 boards, servo, ESC and sensor must share a suitable ground, and the BEC voltage must be verified before connecting the battery.

The current firmware does not implement battery monitoring or a Battery Guardian. No battery-protection feature should be claimed unless calibrated hardware and firmware are added and tested.

## Configuration

Before uploading firmware, print the STA MAC of both boards through the serial monitor.

Set the ESP32-S3 MAC in `ControllerUnit/include/Config.h` using `vehicleUnitAddress`. Set the ESP32-WROVER MAC in `VehicleUnit/include/Config.h` using `controllerUnitAddress`. Neither address may remain all zeros.

The default Controller Unit SD wiring is:

| SD signal | ESP32-WROVER GPIO |
| --- | --- |
| SCK | 18 |
| MISO | 19 |
| MOSI | 23 |
| CS | 5 |

Change these values in `ControllerUnit/include/Config.h` if the actual wiring is different.

## Build

```bash
cd ControllerUnit
pio run

cd ../VehicleUnit
pio run
```

The Controller Unit uses the `esp32dev` PlatformIO target because the selected PS5 library requires Bluetooth Classic. The Vehicle Unit uses the `esp32-s3-devkitc-1` target with a 16 MB flash configuration.

## Safety and security limitations

The Vehicle Unit starts with the steering servo centered and the ESC at neutral. It accepts only valid commands from the configured Controller Unit MAC, rejects old sequence numbers and returns to neutral when no fresh command has been received within the local timeout.

These are research-prototype safeguards, not certified safety controls. The current CRC32 detects accidental corruption but is not authentication. The project does not yet provide authenticated encryption, secure key provisioning, complete replay protection or a certified safety mechanism.

Do not connect the motor or apply battery power until the system has passed a bench test with the wheels lifted or the motor mechanically disconnected.

## Project contribution and dependencies

Minas contributes the application-layer integration of controller telemetry, trial labeling, sequence handling, SD logging, ESP-NOW command transfer and the planned driver-authorization interface. Bluetooth, ESP-NOW, AES, SHA-256 and CRC algorithms are established technologies or library components used by the project; Minas does not claim to have invented them.

## Reproducibility

For every collection session, record the board models, firmware revision, PS5 library revision, both STA MAC addresses, SD card format, telemetry interval, wiring revision, route or task, driver label, session identifier and trial number.