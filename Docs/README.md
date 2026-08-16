# Minas Documentation

## Implemented collection system

The project has two firmware targets. `MinasDT` is the car transmitter. It reads the PS5 controller, controls the servo and ESC, measures sonar distance, and transmits telemetry. `MinasDR` is the base-station receiver. It receives telemetry, stores trial files on an SD card, and returns the protocol acknowledgement.

The current implementation is a data-collection prototype. It does not yet run a driver-classification model and it does not yet send a classifier-generated `CONTINUE` or `STOP` authorization decision.

## Owner/non-owner trials

The owner label is a research ground-truth label selected before a trial. The label is not an authenticated identity claim. Keep one driver and one fixed label in each trial. Pressing Circle changes the label while the car is neutral. When the receiver receives the new label, it closes the current file and opens a new file.

Files are named as follows:

```text
trial_0001_owner.csv
trial_0002_nonowner.csv
```

Do not mix data from two drivers in a single trial file. Keep a separate experiment manifest containing the real driver/session information outside the telemetry file.

## Hardware safety

The HC-SR04 Echo line must not be connected directly to an ESP32 GPIO unless the exact sensor output is confirmed to be 3.3 V safe. Use a voltage divider or level shifter. The ESP32, receiver, servo, ESC and sensor must share a suitable ground, and the BEC voltage must be verified before connection.

Battery monitoring is not implemented in the collection firmware. Remove any claim that a 6.8 V Battery Guardian is active unless the ADC divider and calibrated code have been installed and tested.

## Protocol status

MRP is a project-specific application-layer telemetry protocol. AES-128 and SHA-256 are standard primitives used by the design. The prototype is not production-grade authenticated encryption: AES-ECB, a fixed magic value, firmware-embedded seed material, and incomplete recovery behavior remain limitations.

## Reproducibility

Record the board model, firmware revision, PS5 library revision, receiver MAC, telemetry interval, SD card format, wiring revision, route/task, driver label, and trial number for every collection session.
