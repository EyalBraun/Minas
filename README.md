# Minas: Continuous Driver Authentication Using Behavioral Biometrics & TinyML

[![PlatformIO Build](https://img.shields.io/badge/PlatformIO-ESP32--WROVER-orange.svg)](https://platformio.org/)
[![Firmware](https://img.shields.io/badge/Firmware-C%2B%2B17%20%7C%20FreeRTOS-blue.svg)](https://isocpp.org/)
[![Machine Learning](https://img.shields.io/badge/ML-Python%20%7C%20Random%20Forest%20%7C%20TinyML-brightgreen.svg)](https://scikit-learn.org/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An autonomous cyber-physical security system that continuously authenticates a vehicle driver in real time using **Behavioral Biometrics** and **Edge TinyML**—combining an **ESP32-WROVER** embedded platform for physical RC vehicles with a **Hardware-in-the-Loop / Live Forza Telemetry (`HILM`)** validation environment.

---

## 🎯 Project Goal & Vision

Traditional automotive anti-theft security relies on **one-time static authentication at startup** (physical keys, smart fobs, or immobilizers). Once the vehicle is in motion, static systems cannot detect relay attacks, cloned credentials, or mid-session carjacking.

**Minas** introduces a continuous, zero-trust security gatekeeper based on **how the driver operates the vehicle**:
1. **Neuromotor Behavioral Biometrics:** Every human driver exhibits a unique, subconscious motor signature in their steering micro-corrections, throttle transitions, and braking dynamics.
2. **Track-Invariant Edge AI:** A lightweight machine learning model evaluates short sliding windows of driving control inputs locally in real time, independent of the specific road or track layout.
3. **Active Cyber-Physical Mitigation:** When consecutive windows confirm an unauthorized driver, the system activates an acoustic alarm buzzer and safely neutralizes vehicle control.

---

## 🔄 System Architecture & Data Flow

![System Architecture & Data Flow](docs/images/system_flow.png)


## ⚡ Hardware Architecture & Electrical Schematic

![Electrical Wiring Schematic](docs/images/wiring_schematic.png)

---

## 📁 Repository Structure

```text
Minas/
├── HILM/
│   ├── forza.py             # 20 Hz Forza UDP telemetry logger
│   ├── model.py             # Behavioral biometric feature extraction & model training
│   └── live_detector.py     # Real-time Forza UDP (Port 5000) & Demo Biometric Guard + HUD
├── include/
│   └── Config.h             # ESP32 GPIO pinouts, PWM limits, and system constants
├── src/
│   └── main.cpp             # 20 Hz ESP32 control loop, SDMMC logger, and actuator failsafe
├── docs/
│   └── images/
│       ├── system_flow.png
│       └── wiring_schematic.png
├── platformio.ini           # PlatformIO ESP32-WROVER build configuration
└── README.md                # Project overview and architecture diagrams
```

---

## 📄 License

Distributed under the **MIT License**.
