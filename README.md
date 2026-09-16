# Minas: Continuous Driver Authentication Using TinyML on ESP32

[![PlatformIO Build](https://img.shields.io/badge/PlatformIO-ESP32--WROVER-orange.svg)](https://platformio.org/)
[![Firmware](https://img.shields.io/badge/Firmware-C%2B%2B17%20%7C%20FreeRTOS-blue.svg)](https://isocpp.org/)
[![Machine Learning](https://img.shields.io/badge/ML-Python%20%7C%20Random%20Forest%20%7C%20TinyML-brightgreen.svg)](https://scikit-learn.org/)
[![Hardware](https://img.shields.io/badge/Hardware-ESP32--WROVER%20%7C%20MicroSD%20%7C%20PS5-red.svg)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An autonomous, embedded cyber-physical security system that continuously identifies and authenticates an RC vehicle driver in real time using **Behavioral Biometrics** and **Edge TinyML (Tiny Machine Learning)** running directly on an ultra-low-cost **ESP32-WROVER** microcontroller.

---

## 📑 Table of Contents
1. [Executive Summary & Vision](#-executive-summary--vision)
2. [End-to-End System Architecture & Data Flow](#-end-to-end-system-architecture--data-flow)
3. [Hardware Architecture & Electrical Wiring](#-hardware-architecture--electrical-wiring)
4. [Vehicle Control Scheme (Option A) & PS5 Integration](#-vehicle-control-scheme-option-a--ps5-integration)
5. [Experimental Protocol & 10-Minute Trial Collection](#-experimental-protocol--10-minute-trial-collection)
6. [Telemetry & Sliding-Window Feature Schema](#-telemetry--sliding-window-feature-schema)
7. [Machine Learning Pipeline & Model Training](#-machine-learning-pipeline--model-training)
8. [Firmware Architecture & Hardware Timer Safety](#-firmware-architecture--hardware-timer-safety)
9. [Build, Flash & Verification Guide](#-build-flash--verification-guide)
10. [Repository Structure](#-repository-structure)
11. [Academic References & License](#-academic-references--license)

---

## 🚀 Executive Summary & Vision

Traditional automotive anti-theft security relies on **one-time static authentication at startup** (physical keys, smart fobs, keyless entry, or fingerprint push-buttons). Once the vehicle is in motion, traditional systems suffer from critical security blind spots:
* **Relay Attacks & Fob Cloning:** Stolen or electronically cloned credentials allow unauthorized vehicle operation.
* **Carjacking & Coercion:** If a vehicle is forcibly taken while running, the vehicle cannot determine that the authorized driver is no longer at the controls.
* **Fleet & Commercial Security:** Logistics operators, car-sharing services, and rental fleets cannot continuously verify driver identity without privacy-invasive cameras.

### The Minas Solution: Zero-Trust Continuous Behavioral Biometrics
**Minas** introduces an active, embedded security gatekeeper:
1. **Neuromotor Behavioral Biometrics:** Every human driver possesses a unique, subconscious "motor signature"—fine-grained steering micro-adjustments, trigger pressure gradients, throttle onset rates, and control derivative profiles: $\Delta\text{Steering}$ and $\Delta\text{Throttle}$.
2. **Edge AI / TinyML:** Instead of streaming high-frequency telemetry to a vulnerable remote cloud server (introducing network latency, connectivity dependency, and privacy risks), classification is performed **100% locally on the ESP32-WROVER**.
3. **Active Cyber-Physical Mitigation:** When an unauthorized driver operates the vehicle, the microcontroller triggers an instant hardware failsafe: neutralizes the Electronic Speed Controller (ESC at 1500 µs), centers the steering, and activates an acoustic alarm buzzer.

---

## 🔄 End-to-End System Architecture & Data Flow

The project operates across a two-phase lifecycle: **Phase 1 (Data Collection & Training)** and **Phase 2 (Edge Inference & Cyber-Physical Protection)**.

![System Architecture & Data Flow](docs/images/system_flow.png)

### Operational Phases:
1. **Phase 1 — Ground Truth Data Collection:**
   - The ESP32 samples user inputs and vehicle commands at a strict **20 Hz rate** (every 50 ms).
   - Telemetry is buffered in RAM and streamed directly to an onboard MicroSD card via 1-bit SDMMC.
   - Structured **10\-minute trials** are recorded for the legitimate **Owner** (Circle button) and **Non\-Owner Impostors** (Square button).
   - Completed CSV logs are transferred to a PC for automated sliding-window feature extraction and model training.
2. **Phase 2 — Real-Time Edge Inference:**
   - The trained classifier is compiled into the ESP32 firmware.
   - Live driving dynamics are evaluated across a **2.0\-second sliding window** (40 samples, stepped every 0.5 seconds).
   - If an unauthorized driver is detected, the ESP32 overrides control, shuts down the motor, and sounds the alarm.

---

## ⚡ Hardware Architecture & Electrical Wiring

Reliable operation of high-current actuators alongside an RF-sensitive microcontroller requires clean electrical isolation and a unified ground plane.

![Electrical Wiring Schematic](docs/images/wiring_schematic.png)

### Complete Wiring & Pinout Table

| Peripheral | Board Pin | Electrical Spec | Subsystem Function |
|---|---|---|---|
| **Steering Servo Signal** | `GPIO 25` | 3.3V Logic PWM (50 Hz) | Controls steering angle ($0^{\circ}$ to $180^{\circ}$, neutral $90^{\circ}$). |
| **ESC Throttle Signal** | `GPIO 26` | 3.3V Logic PWM (50 Hz) | Electronic Speed Controller throttle ($1000\,\mu\text{s}$ to $2000\,\mu\text{s}$, neutral $1500\,\mu\text{s}$). |
| **Piezo Buzzer (+)** | `GPIO 32` | 3.3V Square Wave (`safeBeep`) | Acoustic feedback without LEDC timer hijacking. |
| **MicroSD Card Bus** | `GPIO 2, 14, 15` | 1-bit Hardware SDMMC | High-speed logging (D0: GPIO 2, CLK: GPIO 14, CMD: GPIO 15). |
| **Internal SPI PSRAM** | `GPIO 16, 17` | Reserved (Do Not Wire!) | Dedicated to 4MB onboard Pseudo-Static RAM. |
| **Internal SPI Flash** | `GPIO 6–11` | Reserved (Do Not Wire!) | Dedicated to onboard 4MB SPI Flash memory. |

### ⚠️ Critical Electrical Design Rules:
1. **Common Ground (GND) Bus:** All grounds (Main Battery GND, Secondary 5V GND, ESP32 GND, Servo GND, ESC GND, Buzzer GND) **must be tied together** to establish a shared reference potential for PWM signals.
2. **Isolated Servo Power:** **Never power an RC servo from the ESP32 3.3V or 5V USB pin!** High-torque servos draw $1.0\text{A}$ to $2.5\text{A}$ stall current during steering turns. Power the servo from a dedicated **Regulated Secondary 5V supply** (UBEC or separate $5\text{V}$–$6\text{V}$ pack).
3. **ESC BEC Isolation:** When using a dedicated secondary 5V pack for the microcontroller and servo, the red wire ($5\text{V}$ BEC) of the ESC receiver plug must be disconnected/insulated to prevent regulator cross-currents.

---

## 🎮 Vehicle Control Scheme (Option A) & PS5 Integration

Minas uses standard automotive dual-trigger control (**Option A**), configured specifically for the Sony PS5 DualSense controller:

| Control Input | Controller Action | Vehicle Response | Data Mapping Range |
|---|---|---|---|
| **Steering** | **Left Stick (X-Axis)** | Steers front wheels Left / Right | `-128` to `+127` $\rightarrow$ $0^{\circ} \dots 180^{\circ}$ (Center: $90^{\circ}$) |
| **Throttle** | **R2 Analog Trigger** | Progressive Forward Acceleration | `0` to `255` $\rightarrow$ 0.0% to +100.0% ($1500 \dots 2000\,\mu\text{s}$) |
| **Brake / Reverse** | **L2 Analog Trigger** | Progressive Braking / Reverse Drive | `0` to `255` $\rightarrow$ 0.0% to -100.0% ($1500 \dots 1000\,\mu\text{s}$) |
| **Owner Trial** | **Circle (○) Button** | Starts 10-Minute Recorded Owner Trial | Emits $2200\text{ Hz}$ confirmation chime |
| **Non-Owner Trial** | **Square (□) Button** | Starts 10-Minute Recorded Non-Owner Trial | Emits $1200\text{ Hz}$ confirmation chime |
| **Cancel / Finish** | **Cross (✕) Button** | Cancel ($<10\text{ min}$) or Finish ($\ge 10\text{ min}$) | Warning buzz or high victory chime |

### Built-in Deadband & Safety Features:
* **Trigger Deadband (20 units):** Prevents creeping caused by the physical resting weight of the DualSense controller on a flat surface.
* **Steering Deadband (10 units):** Eliminates center stick drift.
* **ESC Neutral Arming:** The firmware holds...
