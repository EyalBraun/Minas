# \# Minas Documentation

# 

# \## Implemented architecture

# 

# Minas currently contains two firmware targets:

# 

# \- \*\*Controller Unit:\*\* ESP32-WROVER with Bluetooth Classic. It connects to the PS5 DualSense, reads steering, throttle, triggers and selected buttons, stores trial files on an external SPI microSD module, and sends commands over ESP-NOW.

# 

# \- \*\*Vehicle Unit:\*\* ESP32-S3 mounted on the car. It validates commands from the configured Controller Unit MAC, drives the steering servo and ESC, reports vehicle telemetry and returns to neutral when a command is denied, invalid, stale or missing.

# 

# The Controller Unit is used for the PS5 connection because the selected DualSense library requires Bluetooth Classic. The Vehicle Unit does not connect to the PS5 controller.

# 

# \## Current project status

# 

# The current firmware is a \*\*research data-collection prototype\*\*. It builds for both targets and supports PS5 input, external SD trial logging, ESP-NOW command transfer, owner/non-owner labels and a Vehicle Unit neutral failsafe.

# 

# A trained driver-classification model is not included. The function `classifyDriver()` is a placeholder. `CONTROLLER\_OPERATION\_MODE=1` is collection bypass and is the mode intended for gathering labeled data. Shadow evaluation and enforcement require a real classifier and separate validation on unseen drivers and sessions.

# 

# \## Owner/non-owner trials

# 

# The target label is binary: `owner` versus `nonowner`. The label is manually assigned as research ground truth for each trial. It is not cryptographic proof of the identity of the person holding the controller.

# 

# One trial must contain one driver, one fixed label and one continuous experimental segment. Pressing Circle toggles the manual label and opens a new CSV trial file on the Controller Unit’s SD card.

# 

# Example files are:

# 

# ```

# /trial\_1\_owner.csv

# /trial\_2\_nonowner.csv

# ```

# 

# Each file contains metadata followed by rows with trial number, input sequence, timestamp, steering, throttle, L2/R2 values, button mask, owner label, decision metadata and Vehicle Unit telemetry.

# 

# For machine-learning evaluation, split the data by driver and session rather than by random rows. Adjacent rows from one trial must not appear in both the training and test sets.

# 

# \## Hardware and wiring safety

# 

# The Controller Unit requires an original ESP32/WROVER-class board, a PS5 DualSense controller and an external SPI microSD module. The Vehicle Unit requires an ESP32-S3-DevKitC-1 N16R8, a steering servo and an ESC. HC-SR04 is optional.

# 

# The default Controller Unit SD wiring is SCK=18, MISO=19, MOSI=23 and CS=5. Change the values in `ControllerUnit/include/Config.h` if the wiring differs.

# 

# The HC-SR04 Echo line must not be connected directly to an ESP32 GPIO if the sensor can output 5 V. Use a voltage divider or level shifter. Verify common ground, BEC voltage, servo power and ESC neutral behavior before applying battery power.

# 

# Battery monitoring and a Battery Guardian are not implemented in the current firmware. They must not be described as active features.

# 

# \## Configuration

# 

# Print the STA MAC of both boards through the serial monitor before upload.

# 

# Set the ESP32-S3 MAC in `ControllerUnit/include/Config.h` using `vehicleUnitAddress`. Set the ESP32-WROVER MAC in `VehicleUnit/include/Config.h` using `controllerUnitAddress`. Do not leave either address as all zeros.

# 

# The Controller Unit also contains a fixed PS5 controller address in `ControllerUnit/include/Config.h`. It must match the DualSense used for the experiment.

# 

# \## Build

# 

# Build the Controller Unit:

# 

# ```bash

# cd ControllerUnit

# pio run

# ```

# 

# Build the Vehicle Unit:

# 

# ```bash

# cd VehicleUnit

# pio run

# ```

# 

# The Controller Unit uses the `esp32dev` PlatformIO target because the selected PS5 library requires Bluetooth Classic. The Vehicle Unit uses `esp32-s3-devkitc-1` with a 16 MB flash configuration.

# 

# \## Bench-test procedure

# 

# Before connecting the motor, lift the wheels or mechanically disconnect the motor. Confirm that the Vehicle Unit starts with the steering servo centered and the ESC at neutral. Confirm that the PS5 connects to the Controller Unit, the SD card opens successfully, a trial file is created, and the Vehicle Unit receives a valid command.

# 

# Then disconnect the Controller Unit or the PS5 and verify that the Vehicle Unit returns to neutral after the local command timeout. Do not begin driving until this test succeeds.

# 

# \## Protocol status

# 

# The current command path uses `shared/MinasProtocol.h`, `AuthorizedVehicleCommand` and `VehicleTelemetry`. CRC32 is used for accidental-corruption detection and is not an authentication tag.

# 

# The old `shared/MRP.cpp`, `shared/MRP.h` and `Docs/MRP\_Specification.md` describe a legacy prototype and are not the command path used by the current Controller Unit and Vehicle Unit build. They should be marked as legacy or replaced with a specification for `shared/MinasProtocol.h`.

# 

# The firmware is not a production authorization system or a certified safety system. Before classifier output is allowed to control the vehicle, authenticated encryption, replay protection, secure provisioning and extensive fail-safe testing are required.

