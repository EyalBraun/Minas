# Minas: Autonomous RC Car Telemetry & Security System

**Author:** Eyal Braun**Platform:** ESP32-S3 & ESP32**Framework:** Arduino / PlatformIO

---

## 1. Project Overview

**Minas** is a professional-grade embedded systems project that transforms a standard RC car into a high-tech data collection and security platform. The system uses a **Dual-Controller Architecture** to perform real-time wireless telemetry, safety monitoring, and driver-recognition using Machine Learning, secured by a custom **Rolling-Key Cryptographic Protocol**.

### **Core Objectives:**

- **Secure Wireless Telemetry:** Real-time encrypted streaming of driving data via **Minas Rolling-Key Protocol (MRP)**.

- **Machine Learning Security:** Building a robust dataset to identify authorized vs. unauthorized drivers based on driving patterns (Steering/Throttle Jerk) with **Gap Padding** for time-series integrity.

- **Obstacle Avoidance:** Integrated sonar-based collision prevention.

---

## 2. The Minas Rolling-Key Protocol (MRP)

To prevent eavesdropping and replay attacks, Minas features a custom bi-directional cryptographic handshake:

1. **Dynamic Key Rotation (Rolling Keys):** Upon every successful packet transmission, the receiver generates a new AES-128 key ($$E_n$$) and securely transmits it back to the car.

1. **Magic Number Validation:** Decrypted payloads are verified against a 32-bit magic constant (`0x4D494E41`) to guarantee decryption accuracy.

1. **Dual-Key Fallback:** The receiver maintains a dual-key state ($$E_m$$ and $$E_n$$) to gracefully handle dropped ACKs without desynchronization.

1. **Sequence Validation:** The transmitter verifies that the incoming acknowledgment matches its exact monotonic counter ($$R_{mc} == T_{mc}$$) before adopting a new key.

For an in-depth mathematical and architectural analysis, see the [MRP Specification Paper](docs/MRP_Specification.md).

---

## 3. System Architecture and Repository Structure

The project is organized into modular components:

- `MinasDT/` - Car Transmitter firmware (ESP32-S3 + PS5 Controller + Sonar + MRP Encryption).

- `MinasDR/` - Base Station Receiver firmware (ESP32 + SD Card + MRP Decryption + ML Feature Engineering).

- `shared/` - Core cryptographic library (`MRP.h`, `MRP.cpp`).

- `docs/` - Technical research papers and wiring diagrams.

---

## 4. Hardware Wiring Diagram

The hardware architecture relies on a stabilized 5V Power Bus supplied by the Electronic Speed Controller (ESC) BEC:

![Minas Complete Wiring Diagram](https://private-us-east-1.manuscdn.com/sessionFile/AqgPQ9OLyKEPD0I9HsZixv/sandbox/uVzjzLPX4nIOJ8FVfL0Zyn-images_1786689950806_na1fn_L2hvbWUvdWJ1bnR1L01pbmFzL2RvY3MvTWluYXNfV2lyaW5nX0NvbXBsZXRl.png?Expires=1786863590&Signature=MEQCIE836GD44SlLXoWG9ecsR6duhnJg7DFfB3yq8Fr-BAYgAiB0I~P4CFeIpJx2pmcTqFxRf5y8J0SUPDUQXz1U69Az8Q__&Key-Pair-Id=K1K5N5YNBUUMMN)

---

**Minas is more than an RC car—it's a research platform for Edge AI, Embedded Systems, and IoT Security.**
