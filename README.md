# Minas WROVER Driver-Recognition Data Collector

This repository contains the **single-board ESP32-WROVER** firmware and offline preprocessing pipeline for the Minas final project: **identifying a driver from the temporal patterns of control dynamics in a robotic car**.

The onboard ESP32-WROVER pairs directly with a Sony PS5 DualSense controller over Bluetooth Classic, reads user inputs, commands the steering servo and ESC, enforces safety failsafes, and logs telemetry samples at 20 Hz to an onboard MicroSD card in 1-bit SDMMC mode.

> **Safety Notice:** This firmware is an experimental research data-collection prototype. Always perform initial hardware tests with the vehicle wheels elevated and the drive motor mechanically disconnected or disabled via `ENABLE_MOTOR_OUTPUT = false`.

---

## Final Project Purpose

The goal of this research project is to build an artificial intelligence system that recognizes who is driving a robotic vehicle based on driving style and control telemetry. 

During 10-minute driving sessions, the ESP32-WROVER logs:
- Analog joystick positions (left stick steering, right stick axes).
- Analog trigger pressures (R2 throttle, L2 brake/reverse).
- Digital button bitmasks.
- Servo angle and ESC pulse width commands.
- Temporal derivatives (steering delta and throttle delta reaction speeds).
- Precise millisecond timestamps and ground-truth driver labels (`owner` vs `nonowner`).

The telemetry data is stored as CSV files on the MicroSD card, transferred to a computer, and processed by `tools_train_driver.py` into sliding time windows. These feature windows are used to train machine learning classifiers (such as Random Forest, Gradient Boosting, SVM, or Neural Networks) capable of distinguishing the vehicle's authorized owner from other drivers.

---

## Hardware, Pinout & Wiring Schematic

### Complete Wiring Schematic

```
                            +--------------------------+
                            |       2S / 3S LiPo       |
                            |       Battery Pack       |
                            +------------+-------------+
                                         |
                       +-----------------+-----------------+
                       | (+) Thick Red   | (-) Thick Black |
                       v                 v                 |
                 +-----------+     +-----------+           |
                 | ESC Power |     | ESC Power |           |
                 |    (+)    |     |    (-)    |           |
                 +-----------+     +-----------+           |
                       |                 |                 |
     +-----------------+-----------------+-----------------+-----------------+
     |                     Electronic Speed Controller (ESC)                 |
     |  Motor Out (3-Phase / DC) --------> Drive Motor                       |
     |  Internal BEC: 5.0 V Output (Red wire on 3-pin receiver cable)        |
     +---------+-------------------------+-------------------------+---------+
               | Signal (White/Orange)   | BEC 5V (Red)            | GND (Black/Brown)
               v                         v                         v
         +-----------+             +-----------+             +-----------+
         |  GPIO 26  |             |  5V Rail  |             | COMMON    |
         +-----+-----+             +-----+-----+             | GROUND    |
               |                         |                   | BUS (GND) |
+--------------+-------------------------+-------------------+-----+-----+---+
|              |                         |                         |         |
|              |     ESP32-WROVER BOARD  |                         |         |
|              |                         +------> VIN / 5V Pin     |         |
|              |                                                   |         |
|  [GPIO 26] --+ (ESC PWM Output, 50 Hz, 1000 - 2000 µs)           |         |
|  [GPIO 25] ----> Steering Servo Signal                           |         |
|  [GPIO 32] ----> Piezo Buzzer (+)                                |         |
|  [GND]     ------------------------------------------------------+         |
|                                                                            |
|  Onboard MicroSD Slot (1-bit SDMMC Mode):                                  |
|    - CLK: GPIO 14                                                          |
|    - CMD: GPIO 15                                                          |
|    - D0:  GPIO 2                                                           |
|                                                                            |
|  Reserved Pins (DO NOT USE):                                               |
|    - PSRAM: GPIO 16, 17 (Dedicated to internal 4MB/8MB SPI PSRAM)         |
|    - Flash: GPIO 6, 7, 8, 9, 10, 11 (Dedicated to SPI Flash memory)        |
+----------------------------------------------------------------------------+
       |                                 |                         |
       | Signal (Orange/White)           | 5V Power (Red)          | GND (Brown/Black)
       v                                 v                         v
+----------------------------------------------------------------------------+
| Steering Servo (High-Torque RC Servo)                                      |
+----------------------------------------------------------------------------+

Piezo Buzzer:
       GPIO 32 ---------------> Positive Lead (+)
       GND -------------------> Negative Lead (-)
```

### Signal & Power Flow (Mermaid Diagram)

```mermaid
flowchart TD
    subgraph Power["Power Subsystem"]
        BAT["LiPo Battery (7.4V - 11.1V)"]
        ESC["Electronic Speed Controller (ESC)"]
        BEC["ESC Built-in BEC (5V / 2-3A)"]
        GND["Common Ground Bus (GND)"]
        BAT -->|V+ Main| ESC
        BAT -->|GND Main| GND
        ESC -->|Regulates| BEC
    end

    subgraph Controller["ESP32-WROVER Mainboard"]
        ESP["ESP32 Microcontroller"]
        SD["Onboard MicroSD Slot (SDMMC 1-Bit)"]
        BT["Bluetooth Classic Receiver"]
        BEC -->|5V Power| ESP
        GND <-->|Reference GND| ESP
        ESP <-->|GPIO 2, 14, 15| SD
    end

    subgraph DriverInput["Wireless Human Control"]
        PS5["Sony PS5 DualSense Controller"]
        PS5 -.->|Bluetooth HID Packets| BT
    end

    subgraph Actuators["Actuation Subsystem"]
        SERVO["Steering Servo"]
        MOTOR["Drive Motor"]
        ESP -->|GPIO 25 (50Hz PWM)| SERVO
        BEC -->|5V Power| SERVO
        GND <-->|GND| SERVO
        ESP -->|GPIO 26 (50Hz PWM)| ESC
        ESC -->|Drive Power| MOTOR
    end

    subgraph Feedback["Acoustic Feedback"]
        BUZZER["Piezo Buzzer"]
        ESP -->|GPIO 32 (Tone)| BUZZER
        GND <-->|GND| BUZZER
    end
```

### Hardware Pin Mapping

| Component | Signal | ESP32 GPIO | Electrical Specification | Wiring & Safety Notes |
|---|---|---|---|---|
| **Steering Servo** | Signal | `GPIO 25` | 3.3V PWM (50 Hz, 0.5–2.5 ms) | Centers at 90° on startup and failsafe. |
| **Steering Servo** | VCC | External 5V | 5.0 V DC (1–2 A peak) | **Never power from ESP32 3.3V pin.** Power from ESC BEC. |
| **Steering Servo** | GND | Common GND | Ground return | Must share common ground with ESP32. |
| **ESC** | Signal | `GPIO 26` | 3.3V PWM (50 Hz, 1.0–2.0 ms) | Neutral is 1500 µs; 1000 µs reverse, 2000 µs full throttle. |
| **ESC** | GND | Common GND | Ground reference | Must share common ground with ESP32. |
| **Piezo Buzzer** | Positive (+) | `GPIO 32` | 3.3V Square Wave | 2200 Hz owner start, 1200 Hz nonowner start, 2500 Hz completion. |
| **Piezo Buzzer** | Negative (-) | Common GND | Ground return | Connected to common ground. |
| **MicroSD Slot** | CLK, CMD, D0 | `GPIO 14, 15, 2`| 3.3V SDMMC (1-bit) | Onboard SDMMC hardware peripheral. |
| **Internal PSRAM** | SPI RAM Bus | `GPIO 16, 17` | Internal WROVER bus | **Reserved for PSRAM.** Never connect external wires! |

---

## Experimental Collection Protocol

The experiment is binary: the car's legitimate owner (`owner`) versus all other drivers (`nonowner`). Every trial is planned for **exactly 10 minutes (600,000 ms)**.

1. **Pairing**: Turn on the ESP32 and pair the PS5 DualSense controller over Bluetooth Classic.
2. **Start a Segment**:
   - Press **Circle**: Starts a 10-minute **`owner`** segment (chimes at 2200 Hz).
   - Press **Square**: Starts a 10-minute **`nonowner`** segment (chimes at 1200 Hz).
3. **During Driving**:
   - The car samples all inputs and outputs at 20 Hz and flushes to the SD card periodically.
   - If the controller disconnects, actuators instantly engage neutral (90° steering, 1500 µs throttle) and failsafe rows (`controller_connected = 0`) are logged.
4. **End of Segment**:
   - **Automatic Completion**: At exactly 10 minutes, the session finalizes, the file is saved, actuators enter failsafe, and a long completion chime (2500 Hz for 500 ms) plays.
   - **Manual Cancel (Cross Button)**: If Cross is pressed before 10 minutes have elapsed, the incomplete trial is **aborted and deleted** from the SD card (low warning buzz at 700 Hz for 300 ms) to keep the dataset pristine.

---

## Logged CSV Schema

Each completed trial file (e.g. `owner_segment_00001.csv`, `nonowner_segment_00002.csv`) starts with metadata lines, followed by 20 telemetry columns sampled at 20 Hz (50 ms):

| Field Group | Field Name | Data Type | Units / Range | Description |
|---|---|---|---|---|
| **Identity & Time** | `segment_number` | unsigned int | 1 .. 99999 | Monotonic global segment ID across all trials |
| | `sample_sequence` | unsigned int | 1 .. N | Sample index within the file |
| | `timestamp_ms` | unsigned long | ms | Time elapsed since ESP32 boot |
| | `elapsed_ms` | unsigned long | ms | Time elapsed since segment start |
| | `label` | string | "owner" / "nonowner" | Ground-truth driver classification |
| | `is_owner` | int | 0 or 1 | Binary ground-truth flag (1 = owner) |
| | `controller_connected`| int | 0 or 1 | 1 = connected, 0 = disconnected failsafe |
| **Raw Controller** | `raw_lx` | int | -128 .. 127 | Left joystick horizontal axis (steering) |
| | `raw_ly` | int | -128 .. 127 | Left joystick vertical axis |
| | `raw_rx` | int | -128 .. 127 | Right joystick horizontal axis |
| | `raw_ry` | int | -128 .. 127 | Right joystick vertical axis |
| | `l2` | int | 0 .. 255 | Left analog trigger (Brake / Reverse) |
| | `r2` | int | 0 .. 255 | Right analog trigger (Throttle) |
| | `buttons_mask` | unsigned int | 16-bit mask | Bitmask of pressed physical buttons |
| **Actuator Commands**| `steering_deg` | float | 0.00 .. 180.00° | Normalized steering intent (90° = center) |
| | `throttle_percent` | float | -100.00 .. +100.00% | Net throttle percentage |
| | `steering_command_deg`| int | 0 .. 180° | Integer angle sent to steering servo |
| | `esc_command_us` | int | 1000 .. 2000 µs | Pulse width sent to ESC (1500 µs = neutral) |
| **Dynamics** | `steering_delta` | float | Δ° / sample | Rate of change of steering command |
| | `throttle_delta` | float | Δ% / sample | Rate of change of throttle command |

---

## Build and Upload

1. Install [PlatformIO](https://platformio.org/) in VS Code.
2. Set your PS5 DualSense controller MAC address in `include/Config.h`.
3. Build and upload:

```bash
pio run
pio run --target upload
pio device monitor
```

---

## Offline Data Processing & ML Windowing

The `tools_train_driver.py` script validates the 16 completed trial files, performs a class-stratified train/test split, copies raw files, and generates sliding-window feature tables:

```bash
python tools_train_driver.py \
  --data-dir /path/to/sd_card_trials \
  --out-dir /path/to/processed \
  --window 40 \
  --stride 10
```

### Output Directories and Files:
* `train_raw/`: 12 raw completed CSV trial files (6 owner, 6 nonowner).
* `test_raw/`: 4 held-out raw completed CSV trial files (2 owner, 2 nonowner).
* `train_window/windows.csv`: Aggregated feature table for training machine learning models.
* `test_window/windows.csv`: Aggregated feature table for evaluating model generalization.
* `split_report.json`: JSON audit log detailing file splits, window counts, and features.

---

## References

[1]: https://github.com/EyalBraun/Minas "EyalBraun/Minas — original repository"
