# Minas: Real-Time Continuous Driver Authentication via Behavioral Biometrics

[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](https://www.python.org/)
[![Machine Learning](https://img.shields.io/badge/ML-Random%20Forest%20%7C%20Compact%20Neural%20Net-brightgreen.svg)](https://scikit-learn.org/)
[![Telemetry](https://img.shields.io/badge/Telemetry-20Hz%20Real--Time%20UDP-orange.svg)](#low-latency-edge-pipeline)
[![Recognition](https://img.shields.io/badge/Award-Top%20100%20National%20Young%20Scientists%20%26%20Developers-gold.svg)](#recognition)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

An independent end-to-end machine learning proof-of-concept investigating whether high-frequency driving telemetry can identify and authenticate drivers in real time **without memorizing track geometry**.

> 🏆 **Recognition:** Built entirely independently without lab resources or mentorship; selected among the **Top 100 projects nationwide (ages 15–20)** at the **Israeli National Young Scientists & Developers Competition**.

---

## 🎯 Problem Statement & Motivation

Traditional automotive security systems rely on **static, one-time authentication at startup** (physical keys, RF fobs, or immobilizers). Once a vehicle is in motion, static mechanisms cannot detect relay attacks, credential cloning, or mid-session unauthorized driver handoffs.

**Minas** introduces a continuous, zero-trust biometric layer based exclusively on **neuromotor driving dynamics**—authenticating the driver every 0.5 seconds by analyzing subconscious micro-corrections in steering, pedal modulation, and transition kinetics.


## 🚀 Quick Start

```bash
# 1. Install dependencies
pip install numpy pandas scikit-learn matplotlib joblib colorama

# 2. Record 20 Hz driving telemetry from Forza Data Out (UDP 127.0.0.1:5000)
python poc/forza_logger.py

# 3. Train models on Tracks A & B and run blind evaluation on Unseen Track D
python poc/train_model.py

# 4. Launch real-time biometric guard (Live UDP stream or Driver Handover Demo)
python poc/live_guard.py --mode demo
```

---

## 📄 License

Distributed under the **MIT License**.
