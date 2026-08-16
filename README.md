# Minas: Owner-Recognition Research Platform

Minas is an embedded research platform for collecting RC-car driving telemetry and later studying whether an authorized owner can be distinguished from other drivers by driving behavior.

## Current architecture

```text
PS5 DualSense
      │ Bluetooth Classic
      ▼
Controller Unit — ESP32-WROVER
      ├── reads controller input
      ├── stores trial data on external microSD
      ├── contains the future ML decision gate
      └── sends an authorized command over ESP-NOW
                    │
                    ▼
Vehicle Unit — ESP32-S3 on the car
      ├── validates the command and CRC
      ├── drives the steering servo and ESC
      └── returns to neutral on timeout or denied command
```

The Controller Unit is an original ESP32/WROVER because the selected DualSense library requires Bluetooth Classic. The Vehicle Unit is the ESP32-S3 and does not connect to the controller; it is the actuator and failsafe node on the car.

## Current firmware scope

The Controller Unit reads steering, throttle, triggers and selected buttons from the DualSense. It stores samples in separate trial files on an external SPI microSD module. Pressing Circle switches the manually assigned ground-truth label between `owner` and `nonowner` and opens a new trial file.

The current `classifyDriver()` function is a clearly marked placeholder. `CONTROLLER_OPERATION_MODE=1` is a collection bypass, `CONTROLLER_OPERATION_MODE=2` is intended for shadow evaluation, and `CONTROLLER_OPERATION_MODE=3` is intended for future enforcement. This release does not contain a trained ML model.

The Vehicle Unit never executes a command merely because it arrived from the controller. It accepts only a valid `AuthorizedVehicleCommand` from the configured Controller Unit MAC, rejects old sequences, and returns the servo and ESC to neutral when no fresh command has arrived within the command timeout.

## Data collection for binary classification

The intended target is a binary label: `owner` versus `nonowner`. The label is manually assigned during data collection and remains fixed for each trial. The CSV contains trial number, sequence, timestamp, steering, throttle, L2/R2 values, button mask, label, decision metadata and Vehicle Unit telemetry.

The rows are intended to be grouped into time windows during offline preprocessing. Train, validation and test splits must be performed by driver and session, not by random individual rows, to avoid leakage between adjacent samples from the same drive.

See `ML_DATASET_ASSESSMENT_HE.md` for the data-quality assessment and recommended evaluation method.

## Hardware prerequisites

The Controller Unit requires an original ESP32/WROVER-class board with Bluetooth Classic, an external SPI microSD module and the DualSense controller. The Vehicle Unit requires the ESP32-S3-DevKitC-1 N16R8 and connections for the steering servo, ESC and optional HC-SR04.

The HC-SR04 Echo line must not be connected directly to an ESP32 GPIO if it can output 5 V. Use a voltage divider or level shifter. Confirm the ESC neutral pulse and all GPIO assignments with the actual hardware before powering the motor.

## Configuration before upload

Print the STA MAC of each board through the serial monitor. Put the ESP32-S3 MAC in `ControllerUnit/include/Config.h` as `vehicleUnitAddress`, and put the WROVER MAC in `VehicleUnit/include/Config.h` as `controllerUnitAddress`. Do not leave either address as all zeros.

The Controller Unit SD defaults are SCK 18, MISO 19, MOSI 23 and CS 5. Change these values if the physical module is wired differently.

## Build

Build the Controller Unit:

```bash
cd ControllerUnit
pio run
```

Build the Vehicle Unit:

```bash
cd VehicleUnit
pio run
```

The Controller Unit uses the `esp32dev` PlatformIO target because the selected PS5 library requires Bluetooth Classic. The Vehicle Unit uses `esp32-s3-devkitc-1`.

## Data files

The Controller Unit creates files such as:

```text
/trial_1_owner.csv
/trial_2_nonowner.csv
```

Each file contains metadata followed by rows with the schema:

```text
trial_number,input_sequence,timestamp_ms,steering,throttle,l2,r2,buttons_mask,owner_label,ml_decision,confidence_permille,allow_motion,vehicle_sonar_cm,vehicle_failsafe
```

## Security status

`shared/MinasProtocol.h` defines a versioned command format and CRC32 for accidental corruption detection. CRC32 is not authentication. Before using the system as a security or authorization product, add authenticated encryption, key management, replay protection and a tested allowlist. The firmware is not a certified safety system.
