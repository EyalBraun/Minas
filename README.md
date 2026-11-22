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

---

## 🔬 Out-of-Sample Experimental Design

To prove that the classifier learns **human behavioral biometrics** rather than memorizing specific corners or route layouts, the experimental protocol enforces strict spatial separation:

* **High-Frequency Telemetry Sampling:** Sampled steering angle, throttle position, braking pressure, and first-order temporal derivatives ($\frac{d\text{Throttle}}{dt}$, $\frac{d\text{Steering}}{dt}$) at **20 Hz** across **2.0-second sliding windows** (40 samples per window, 0.5-second stride).
* **Multi-Driver Dataset:** Recorded ~30 minutes of active driving telemetry per participant across 3 distinct drivers in *Forza Horizon*.
* **Strict Spatial Separation (Zero Data Leakage):**
  * **Training & Threshold Calibration Set:** Tracks **A & B** (~20 minutes per driver; 9,510 extracted windows). Decision thresholds and feature normalizers (`StandardScaler`) are fitted exclusively via stratified out-of-fold cross-validation on Tracks A & B.
  * **Held-Out Blind Evaluation Set:** Unseen Track **D** (~10 minutes per driver; 3,571 extracted windows), evaluated strictly out-of-sample with locked decision thresholds.

---

## 🧠 Track-Invariant Neuromotor Feature Engineering

Rather than feeding raw coordinates, vehicle speed, or lap timestamps into the model, Minas extracts **16 track-invariant behavioral features** from each 2.0-second window:

| Category | Extracted Behavioral Signatures |
| :--- | :--- |
| **Steering Micro-Corrections** | Mean steering rate ($\overline{\|\dot{\delta}\|}$), steering rate standard deviation ($\sigma_{\dot{\delta}}$), peak correction impulse ($\max\|\dot{\delta}\|$), 90th-percentile correction sharpness, and zero-crossing direction reversal frequency |
| **Steering Envelope** | Mean absolute steering magnitude ($\overline{\|\delta\|}$) and angular dispersion ($\sigma_{\delta}$) |
| **Throttle Dynamics** | Mean throttle commitment ($\overline{T}$), throttle variability ($\sigma_{T}$), mean pedal transition rate ($\overline{\|\dot{T}\|}$), pedal rate dispersion ($\sigma_{\dot{T}}$), and full-throttle commitment ratio ($T > 0.85$) |
| **Braking & Coasting Kinetics** | Mean braking intensity ($\overline{B}$), braking dispersion ($\sigma_{B}$), active braking engagement ratio ($B > 0.10$), and zero-pedal coasting ratio ($T < 0.05 \land B < 0.05$) |

---

## 📊 Model Benchmarking & Performance (Unseen Track D)

We benchmarked a **Random Forest** classifier against a **Compact Neural Network (MLP)** on the completely unseen Track D using continuous rolling consensus evaluation:

| Model Classifier | Calibrated Threshold (Locked on Tracks A & B) | Accuracy (Target > 90%) | False Acceptance Rate — FAR (Target < 5%) | False Rejection Rate — FRR (Target < 10%) |
| :--- | :---: | :---: | :---: | :---: |
| **Compact Neural Network** | **0.46** | **94.0%** ✅ | **4.2%** ✅ | **9.5%** ✅ |
| **Random Forest** | **0.47** | **91.0%** ✅ | **5.7%** | **15.7%** |

* **Accuracy (94.0%):** Overall correct continuous classification on an unfamiliar road layout.
* **FAR — False Acceptance Rate (4.2%):** Percentage of unauthorized driving windows falsely accepted as the owner (primary anti-theft security metric).
* **FRR — False Rejection Rate (9.5%):** Percentage of authorized owner windows falsely flagged as anomalous.

---

## ⚡ Low-Latency Edge Pipeline

The repository implements a complete local telemetry and inference pipeline with **zero cloud dependency**:

1. **[`poc/forza_logger.py`](poc/forza_logger.py) — 20 Hz UDP Telemetry Interceptor:**
   Captures raw binary *Forza Data Out* UDP packets (Port 5000) with non-blocking OS socket buffer draining to eliminate queuing latency, normalizes control channels, computes real-time temporal derivatives, and exports structured CSV runs.
2. **[`poc/train_model.py`](poc/train_model.py) — Out-of-Sample Training & Calibration Engine:**
   Filters non-driving idle intervals, extracts the 16 neuromotor features, performs stratified 5-fold cross-validation threshold calibration on Tracks A & B, evaluates blind generalization on Track D, and exports the serialized model bundle.
3. **[`poc/live_guard.py`](poc/live_guard.py) — Real-Time Biometric Guard & Handover Simulator:**
   Maintains a 40-sample circular buffer at 20 Hz, runs local inference every 0.5 seconds, and triggers an acoustic & visual alarm within **4.0 seconds** (8 consecutive anomalous windows) of an unauthorized driver taking control.

---

## 📁 Repository Structure

```text
Minas/
├── poc/
│   ├── forza_logger.py          # 20 Hz UDP telemetry recorder & derivative calculator
│   ├── train_model.py           # Out-of-sample feature extraction, calibration & benchmarking
│   └── live_guard.py            # Real-time UDP inference engine, HUD & driver handoff demo
├── .gitignore                   # Excludes local raw telemetry CSVs and serialized artifacts
└── README.md                    # Experimental methodology, benchmarks & pipeline documentation
```

---

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
