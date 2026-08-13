# Minas: Autonomous RC Car Telemetry & Security System

**Author:** Eyal Braun**Platform:** ESP32-S3 & ESP32**Framework:** Arduino / PlatformIO

---

## 1. Project Overview

**Minas** is a professional-grade embedded systems project that transforms a standard RC car into a high-tech data collection and security platform. The system uses a **Dual-Controller Architecture** to perform real-time wireless telemetry, safety monitoring, and driver-recognition using Machine Learning.

### **Core Objectives:**

- **Wireless Telemetry:** Real-time streaming of driving data via ESP-NOW.

- **Machine Learning Security:** Building a dataset to identify authorized vs. unauthorized drivers based on driving patterns (Steering/Throttle Jerk).

- **Battery Guardian:** Active voltage monitoring and emergency shutdown for LiPo safety.

- **Obstacle Avoidance:** Integrated sonar-based collision prevention.

---

## 2. System Architecture

The project is divided into two main components:

### **A. MinasDT (Data Transmitter - The Car)**

The "Brain" of the vehicle, running on an **ESP32-S3 (N16R8)**.

- **Control:** Interfaces with a PS5 Controller via Bluetooth.

- **Actuation:** Controls Steering Servo and Brushless ESC (Electronic Speed Controller).

- **Sensors:** HC-SR04 Ultrasonic sensor for obstacle detection and ADC for battery voltage sensing.

- **Communication:** Broadcasts telemetry packets every 100ms using the **ESP-NOW** protocol.

### **B. MinasDR (Data Receiver - The Base Station)**

The "Black Box" of the system, running on an **ESP32 with SD Card support**.

- **Reception:** Listens for telemetry packets from the car.

- **Data Integrity:** Uses a **Monotonic Counter** to detect packet loss and performs **Gap Padding** to maintain a consistent time-series dataset.

- **Feature Engineering:** Calculates real-time ML features (Jerk, Correlation) before logging.

- **Storage:** Saves all data into a centralized `/minas_master_dataset.csv` for ML training.

---

## 3. Hardware Requirements

| Component | Specification | Purpose |
| --- | --- | --- |
| **Microcontroller (Car)** | ESP32-S3 N16R8 | Main Logic & AI Vector acceleration |
| **Microcontroller (Base)** | ESP32 Wrover | Telemetry Reception & SD Logging |
| **Battery** | 2S 7.4V 2500mAh LiPo | Main Power Source |
| **Sensors** | HC-SR04 Ultrasonic | Obstacle Avoidance |
| **Storage** | Micro SD Card (FAT32) | Dataset Collection |
| **Charger** | ToolkitRC M4 Pocket | LiPo Maintenance |

---

## 4. Machine Learning Feature Schema

The system generates a professional CSV dataset with the following features for driver identification:

- **Sequence:** Monotonic packet counter.

- **SteerJerk:** Rate of change in steering angle (Detects twitchy vs. smooth drivers).

- **ThrottleJerk:** Rate of change in acceleration.

- **STCorr:** Steering-Throttle correlation (Detects cornering habits).

- **PacketLost:** Flag for handling signal interference.

---

## 5. Safety Features

1. **Battery Guardian:** If battery voltage drops below **6.8V**, the car disables the motor and emits a high-pitched siren to prevent cell damage.

1. **Obstacle Fail-safe:** Forward motion is automatically blocked if an obstacle is detected within **30cm**.

1. **Bluetooth Fail-safe:** If the PS5 controller disconnects, the car immediately returns to neutral.

---

## 6. Future Roadmap

- **Phase 1:** Integration of an **IMU (MPU6050)** for 6-axis G-force telemetry.

- **Phase 2:** Deployment of the **TensorFlow Lite** model directly on the ESP32-S3 for real-time unauthorized driver detection.

- **Phase 3:** Vision-based lane following using **ESP32-CAM**.

---

**Minas is more than an RC car—it's a research platform for Edge AI and Embedded Systems.**
