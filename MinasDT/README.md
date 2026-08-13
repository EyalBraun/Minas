# MinasDT (Data Transmitter)

**Author:** Eyal Braun & Manus AI**Project:** Minas - Autonomous RC Car Telemetry & Security System

---

## 1. Overview

**MinasDT** is the firmware for the Minas RC car. It runs on an ESP32-S3 and handles real-time control via a PS5 controller, obstacle avoidance using ultrasonic sensors, and professional-grade battery safety monitoring. Instead of logging data locally, it broadcasts real-time telemetry to the **MinasDR** base station using the **ESP-NOW** protocol.

---

## 2. Key Features

- **ESP-NOW Telemetry:** Streams driving data (steering, throttle, sonar) wirelessly to a receiver.

- **Battery Guardian:** Continuously monitors 2S LiPo voltage. If the voltage drops below **6.8V**, the car immediately shuts down the motor and emits a high-pitched siren to prevent battery damage.

- **Obstacle Avoidance:** Automatically blocks forward movement if an object is detected within 30cm.

- **Dual Driver Modes:** Supports "Owner" and "Guest" modes (toggled via the Circle button) for training driver-recognition Machine Learning models.

---

## 3. Hardware Configuration

### **Pinout Table**

| Component | ESP32 Pin | Function |
| --- | --- | --- |
| **Steering Servo** | GPIO 25 | PWM Control |
| **ESC (Motor)** | GPIO 26 | PWM Control |
| **Buzzer** | GPIO 27 | Audio Feedback |
| **Sonar Trig** | GPIO 22 | Ultrasonic Trigger |
| **Sonar Echo** | GPIO 23 | Ultrasonic Echo |
| **Battery Sense** | GPIO 34 | ADC Voltage Monitoring |

---

## 4. Setup Instructions

1. **MAC Address:** Open `include/Config.h` and replace `receiverAddress` with the MAC address of your **MinasDR** receiver board.

1. **Voltage Divider:** Ensure you have a voltage divider (e.g., two 10k resistors) connected between the battery and GPIO 34 to safely step down the 7.4V-8.4V to 3.3V.

1. **Upload:** Use PlatformIO to compile and upload the code to your ESP32-S3.

---

## 5. Telemetry Schema

Data is sent in a binary struct for maximum speed:

- `sequenceNumber`: Monotonic counter for packet loss detection.

- `timestamp`: Milliseconds since boot.

- `throttle`: Raw command (-100 to 100).

- `steering`: Raw angle (0 to 180).

- `sonarDistance`: Distance to front obstacle.

- `packetLost`: Flag for receiver-side padding.