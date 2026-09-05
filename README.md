# Minas WROVER Driver-Recognition Data Collector

This directory is the **single-board ESP32-WROVER version** of Minas, adapted for the proposed final project: identifying a driver from the temporal pattern of the driver’s controls in a robotic car. The WROVER connects directly to a PS5 DualSense controller over Bluetooth Classic, reads an onboard microSD card, drives the steering/ESC outputs, and measures an HC-SR04-compatible ultrasonic sonar.

> **Scope:** This firmware is a research data-collection prototype. It is not a certified autonomous-driving, anti-theft, or safety-critical identity system. The first hardware tests must be performed with the wheels lifted and the motor mechanically disconnected.

## What was selected and removed

The original repository separates a WROVER Controller Unit from an ESP32-S3 Vehicle Unit. This directory intentionally consolidates the relevant behavior into **one ESP32-WROVER board**: PS5 input, SD logging, actuator commands, sonar sampling, and neutral failsafe behavior. The original two-board MRP/ESP-NOW transport is not required for this single-board data-collection experiment.

The selected version includes the **sonar** and does **not** include a Shadow Stack or Rewind subsystem. It also does not pretend that the label is an inferred identity: `driver_id` and `session_id` are experimental ground-truth metadata supplied by the researcher. The on-device firmware performs no driver classification; the offline Python pipeline prepares windows for later model training.

## מטרת פרויקט הגמר

המטרה היא לבנות מערכת שמזהה מי נוהג במכונית רובוטית לפי דפוסי השליטה שלו. בזמן נהיגה ה-WROVER אוסף את מיקום הג'ויסטיקים, עוצמת הטריגרים, הכפתורים, פקודות הסרוו וה-ESC, קצב השינוי בפקודות, מרחק ה-sonar, חותמות זמן, מזהה הנהג ומזהה הסשן. הנתונים נשמרים כקובצי CSV בכרטיס microSD, מועברים למחשב, עוברים ניקוי וחלוקה לחלונות זמן רציפים, ומשמשים לאימון ולהערכה של מודל למידת מכונה.

המערכת הסופית המתוכננת היא אתר שאליו המשתמש יעלה חלון נתונים חדש. האתר יחזיר את הנהג המשוער, רמת ביטחון, וכן תשובה מפורשת מסוג **unknown / insufficient confidence** כאשר אין ודאות מספקת. התיקייה הזו מכינה את שכבת איסוף הנתונים ואת עיבוד-הקדם; היא אינה טוענת שכבר קיים בה מודל מאומן או אתר פרודקשן.

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
     |  Internal BEC: 5.0 V Output (Red wire on 3-pin servo cable)           |
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
|  [GPIO 27] ----> Sonar Trigger (TRIG)                            |         |
|  [GPIO 33] <---- Sonar Echo (ECHO) via Voltage Divider           |         |
|  [GPIO 32] ----> Piezo Buzzer (+)                                |         |
|  [GND]     ------------------------------------------------------+         |
|                                                                            |
|  Onboard MicroSD Slot (1-bit SDMMC Mode):                                  |
|    - CLK: GPIO 14                                                          |
|    - CMD: GPIO 15                                                          |
|    - D0:  GPIO 2                                                           |
|                                                                            |
|  Reserved Pins (DO NOT USE):                                               |
|    - PSRAM: GPIO 16, 17                                                    |
|    - Flash: GPIO 6, 7, 8, 9, 10, 11                                        |
+----------------------------------------------------------------------------+
       |                                 |                         |
       | Signal (Orange/White)           | 5V Power (Red)          | GND (Brown/Black)
       v                                 v                         v
+----------------------------------------------------------------------------+
| Steering Servo (High-Torque RC Servo)                                      |
+----------------------------------------------------------------------------+

HC-SR04 Ultrasonic Sonar (With 5V to 3.3V Voltage Divider Protection):
       +5V Rail --------------> VCC (Pin 1)
       GPIO 27 ---------------> TRIG (Pin 2)
       GND -------------------> GND (Pin 4)
       
       ECHO (Pin 3, 5V) ----[ 1 kΩ Resistor ]----+-----> GPIO 33 (3.33V safe!)
                                                 |
                                          [ 2 kΩ Resistor ]
                                                 |
                                                GND

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
        PS5 -.->|Bluetooth HID Data| BT
    end

    subgraph Actuators["Actuators & Steering"]
        SERVO["Steering Servo"]
        MOTOR["Drive Motor"]
        ESP -->|GPIO 25 (50Hz PWM)| SERVO
        BEC -->|5V Power| SERVO
        GND <-->|GND| SERVO
        ESP -->|GPIO 26 (50Hz PWM)| ESC
        ESC -->|Phase Power| MOTOR
    end

    subgraph Sensors["Sensing & Feedback"]
        SONAR["HC-SR04 Ultrasonic Sensor"]
        DIVIDER["Voltage Divider: 1kΩ / 2kΩ"]
        BUZZER["Piezo Buzzer"]
        ESP -->|GPIO 27 (Trig)| SONAR
        BEC -->|5V Power| SONAR
        GND <-->|GND| SONAR
        SONAR -->|Echo 5V Pulse| DIVIDER
        DIVIDER -->|Scaled 3.3V Pulse| ESP
        ESP -->|GPIO 32 (Tone)| BUZZER
        GND <-->|GND| BUZZER
    end
```

### Complete Hardware Pin Mapping

| Component | Pin / Signal | ESP32 GPIO | Electrical Spec | Safety & Wiring Notes |
|---|---|---|---|---|
| **Steering Servo** | Signal | `GPIO 25` | 3.3V PWM (50 Hz, 0.5-2.5 ms) | Centers at 90° on boot and failsafe. |
| **Steering Servo** | VCC / Red | External 5V | 5.0 V (1–2 A peak) | **Never power from ESP32 3.3V pin.** Power from ESC BEC. |
| **Steering Servo** | GND / Brown | Common GND | Ground return | Must share ground with ESP32. |
| **ESC** | Signal | `GPIO 26` | 3.3V PWM (50 Hz, 1.0-2.0 ms) | Neutral is 1500 µs. Tested with wheels lifted. |
| **ESC** | GND | Common GND | Ground reference | Must share ground with ESP32. |
| **HC-SR04 Sonar** | VCC | External 5V | 5.0 V DC | Powers the ultrasonic transducers. |
| **HC-SR04 Sonar** | TRIG | `GPIO 27` | 3.3V Digital Output | 10 µs high trigger pulse every 100 ms. |
| **HC-SR04 Sonar** | ECHO | `GPIO 33` | 3.3V Digital Input | **Pass through voltage divider** (1kΩ series, 2kΩ to GND). |
| **HC-SR04 Sonar** | GND | Common GND | Ground return | Ground of voltage divider and sonar. |
| **Piezo Buzzer** | Positive (+) | `GPIO 32` | 3.3V Square Wave | 2200 Hz for owner, 1200 Hz for nonowner. |
| **Piezo Buzzer** | Negative (-) | Common GND | Ground return | Connect to common ground. |
| **MicroSD Slot** | CLK, CMD, D0 | `GPIO 14, 15, 2` | 3.3V SDMMC (1-bit) | Onboard SDMMC hardware peripheral. |
| **Internal PSRAM** | SPI RAM Bus | `GPIO 16, 17` | Internal WROVER bus | **Reserved for PSRAM.** Never connect wires to GPIO 16/17! |

> [!CAUTION]
> **5V Voltage Divider Requirement**: The HC-SR04 Echo pin outputs a 5.0 V logic pulse. Connecting 5.0 V directly to ESP32 GPIO 33 will cause electrical damage. Always use a voltage divider (1 kΩ in series with Echo, and 2 kΩ pulled down to GND) to produce a safe 3.33 V signal.

---

## Collection protocol

This version uses a binary experimental protocol: the car's owner (`owner`) versus every other driver (`nonowner`).

1. **Pairing**: Turn on the ESP32 and pair the PS5 DualSense controller over Bluetooth.
2. **Start / Switch Segment**: Press **Circle**.
   - First press opens `owner_segment_00001.csv` and plays a high buzzer chime (2200 Hz).
   - Subsequent presses close the active file, switch to `nonowner` (or back to `owner`), create the next segment (e.g. `nonowner_segment_00002.csv`), and play a lower chime (1200 Hz).
3. **End Segment**: Press **Cross**. The current file flushes and closes, and actuators return to neutral failsafe.
4. **Failsafe Behavior**: If the controller disconnects or goes out of range at any point, the steering immediately centers (90°), the motor commands neutral (1500 µs), and failsafe rows (`controller_connected = 0`) are written until connection is restored.

---

## Logged CSV schema

Each trial recorded on the MicroSD card contains self-describing metadata headers, followed by 22 columns sampled at 20 Hz (50 ms):

| Field Group | Field Name | Data Type | Units / Range | Description |
|---|---|---|---|---|
| **Identity & Time** | `segment_number` | unsigned int | 1 .. 99999 | Monotonic global segment ID |
| | `sample_sequence` | unsigned int | 1 .. N | Sample index within the file |
| | `timestamp_ms` | unsigned long | ms | Time elapsed since ESP32 boot |
| | `elapsed_ms` | unsigned long | ms | Time elapsed since segment start |
| | `label` | string | "owner" / "nonowner" | Ground-truth driver classification |
| | `is_owner` | int | 0 or 1 | Binary ground truth (1 = owner) |
| | `controller_connected`| int | 0 or 1 | 1 = connected, 0 = disconnected failsafe |
| **Raw Controller** | `raw_lx` | int | -128 .. 127 | Left joystick horizontal axis (steering) |
| | `raw_ly` | int | -128 .. 127 | Left joystick vertical axis |
| | `raw_rx` | int | -128 .. 127 | Right joystick horizontal axis |
| | `raw_ry` | int | -128 .. 127 | Right joystick vertical axis |
| | `l2` | int | 0 .. 255 | Left analog trigger (Brake / Reverse) |
| | `r2` | int | 0 .. 255 | Right analog trigger (Throttle) |
| | `buttons_mask` | unsigned int | 16-bit mask | Bitmask of pressed buttons |
| **Actuator Commands**| `steering_deg` | float | 0.00 .. 180.00° | Normalized steering intent (90° = center) |
| | `throttle_percent` | float | -100.00 .. +100.00% | Net throttle percentage |
| | `steering_command_deg`| int | 0 .. 180° | Angle sent to steering servo |
| | `esc_command_us` | int | 1000 .. 2000 µs | Pulse sent to ESC (1500 µs = neutral) |
| **Dynamics** | `steering_delta` | float | Δ° / sample | First derivative of steering command |
| | `throttle_delta` | float | Δ% / sample | First derivative of throttle command |
| **Context & Quality**| `sonar_distance_cm` | int | 2 .. 450 cm | Distance to front obstacle (-1 = no echo) |
| | `sonar_valid` | int | 0 or 1 | 1 = valid echo received, 0 = timeout |

---

## Build and upload

1. Open the project folder in VS Code with PlatformIO installed.
2. Update your PS5 DualSense controller MAC address in `include/Config.h`.
3. Compile and flash:

```bash
pio run
pio run --target upload
pio device monitor
```

---

## Offline Feature Processing & CSV Windowing

The `tools_train_driver.py` script reads exported trial CSV files, segments continuous time-series into fixed sliding windows (default 40 samples ≈ 2 seconds with a 10-sample stride), computes statistical features across all metrics, and exports clean **tabular CSV files** ready for machine learning:

```bash
python tools_train_driver.py \
  --data-dir /path/to/exported_trials \
  --out-dir /path/to/processed \
  --window 40 \
  --stride 10 \
  --test-ratio 0.25
```

### Generated CSV Outputs
* `windows_train.csv`: Training dataset containing stratified complete segments.
* `windows_test.csv`: Evaluation dataset containing unseen held-out segments.
* `collection_report.json`: Audit log of file distributions, parameters, and segment splits.

### CSV Window Feature Columns
Each row in `windows_train.csv` and `windows_test.csv` represents one window:
1. **Window Identifiers**: `window_id`, `source_segment`, `label`, `is_owner`, `start_timestamp_ms`, `end_timestamp_ms`, `duration_ms`, `sample_count`
2. **Aggregated Feature Statistics** (4 features per metric: `_mean`, `_std`, `_min`, `_max`):
   - `raw_lx`, `raw_ly`, `raw_rx`, `raw_ry`, `l2`, `r2`
   - `steering_deg`, `throttle_percent`, `steering_command_deg`, `esc_command_us`
   - `steering_delta`, `throttle_delta`, `sonar_distance_cm`
3. **Data Quality Ratios**:
   - `sonar_valid_ratio`: Fraction of valid sonar echoes within the window (0.0 to 1.0).
   - `controller_connected_ratio`: Fraction of samples where controller remained connected (0.0 to 1.0).

#### Example Window CSV Row
```csv
window_id,source_segment,label,is_owner,start_timestamp_ms,end_timestamp_ms,duration_ms,sample_count,raw_lx_mean,raw_lx_std,raw_lx_min,raw_lx_max,steering_deg_mean,steering_deg_std,steering_deg_min,steering_deg_max,throttle_percent_mean,throttle_percent_std,throttle_percent_min,throttle_percent_max,steering_command_deg_mean,steering_command_deg_std,steering_command_deg_min,steering_command_deg_max,esc_command_us_mean,esc_command_us_std,esc_command_us_min,esc_command_us_max,steering_delta_mean,steering_delta_std,steering_delta_min,steering_delta_max,throttle_delta_mean,throttle_delta_std,throttle_delta_min,throttle_delta_max,sonar_distance_cm_mean,sonar_distance_cm_std,sonar_distance_cm_min,sonar_distance_cm_max,sonar_valid_ratio,controller_connected_ratio
0,owner_segment_00001.csv,owner,1,110000.0,111950.0,1950.0,40,12.5,4.2,4.0,22.0,98.82,2.96,92.82,105.53,35.2,8.4,18.0,52.0,99.0,3.1,93.0,106.0,1676.0,42.0,1590.0,1760.0,0.32,1.15,-1.41,2.82,0.85,2.1,-3.5,4.2,142.3,12.1,120.0,165.0,1.0,1.0
```

## References

[1]: https://github.com/EyalBraun/Minas "EyalBraun/Minas — original repository"
