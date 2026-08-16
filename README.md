# Minas: ESP32 RC-Car Telemetry and Owner/Non-Owner Research Platform

Minas is an embedded research platform for collecting driving telemetry from an RC car and studying whether the authorized owner can be distinguished from other drivers by driving behavior.

## Current tested scope

The current collection firmware supports the following workflow:

- `MinasDT` runs on a classic ESP32 board, reads a PS5 DualSense controller over Bluetooth Classic, controls a steering servo and ESC, measures distance with an HC-SR04, and sends encrypted telemetry over ESP-NOW.
- `MinasDR` runs on an ESP32 receiver with SD_MMC storage, decrypts valid telemetry, detects sequence gaps, and creates a separate CSV file for each fixed owner/non-owner trial.
- Pressing Circle changes the collection label. The car must be at neutral when changing modes. The receiver opens a new file such as `trial_0001_owner.csv` or `trial_0002_nonowner.csv` when the new label is received.
- The CSV files are intended for research data collection. They are not yet the output of an ML classifier.

## Important hardware decision

The PS5 library used by this version requires Bluetooth Classic. The data-transmitter board must therefore be a classic ESP32 board such as `esp32dev`. An ESP32-S3 cannot use this Bluetooth Classic library. To keep an ESP32-S3, replace the controller solution with a USB-host or BLE-compatible DualSense implementation and adapt the input code separately.

## Before uploading

Replace the six zero bytes in `MinasDT/include/Config.h` with the real Wi-Fi station MAC address of MinasDR. Install a voltage divider or level shifter on the HC-SR04 Echo line before connecting it to GPIO23; a standard HC-SR04 Echo output may be 5 V and can damage an ESP32 input.

The current collection version deliberately does not claim to implement a battery guardian. The battery ADC constants are disabled until a physical voltage divider is installed and calibrated with a multimeter. Do not rely on software battery protection until that feature is implemented and tested.

## Trial files

Each file contains metadata comments followed by the CSV header:

```text
# Minas telemetry trial
# label=owner
# start_sequence=123
# firmware_version=collection-v1
Sequence,TimestampMs,Throttle,Steering,SonarCm,PacketLossCountBefore,IsOwner,DeltaTimeMs,SteeringRate,ThrottleRate,SteeringChange,ThrottleChange,SteeringThrottleProduct
```

A missing packet is represented by the loss count on the next received row. The receiver does not invent zero-valued training rows.

## Protocol status

The Minas Rolling-Key Protocol is an original application-layer protocol design for this project. It uses AES-128 and SHA-256 as established cryptographic building blocks. The current version adds length checks and correct AES-block transmission lengths, but it remains a prototype: it uses AES-ECB and a magic number rather than authenticated encryption. Do not use it as a production authorization protocol or as the sole safety mechanism.

## Build

Build the receiver from `MinasDR`:

```bash
pio run
```

Build the transmitter from `MinasDT`:

```bash
pio run
```

The transmitter uses `huge_app.csv` because the PS5 library makes the firmware larger than the default application partition.

## Safe test order

Test first with the motor mechanically disconnected or with the driven wheels raised. Verify neutral on boot, controller disconnect, receiver timeout, and sonar obstacle detection. Then verify that the receiver creates the first CSV file and that switching modes creates the next trial file. Only after these checks should the car be tested on the ground.
