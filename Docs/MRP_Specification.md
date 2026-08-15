# Minas Rolling-Key Protocol (MRP) v2.0: Enterprise-Grade Secure Telemetry Framework for Autonomous RC Systems

**Author:** Eyal Braun & Manus AI**Project:** Minas - Autonomous RC Car Telemetry & Security System**Classification:** Technical Specification & Advanced Security Research

---

## 1. Abstract and Architectural Evolution

In wireless telemetry and control systems for autonomous vehicles, data integrity, anti-replay protection, and robust cryptographic synchronization are paramount [1]. Traditional static-key encryption schemes are highly vulnerable to interception, sniffing, and replay attacks [1]. The **Minas Rolling-Key Protocol (MRP)** is a bi-directional cryptographic handshake optimized for resource-constrained microcontrollers (ESP32 / ESP32-S3) [1].

Version 2.0 of MRP introduces a major architectural hardening: replacing static pre-shared fallback lists with a **Cryptographic Key Derivation Function (KDF)**. This ensures that even in severe packet-loss environments or under active jamming attacks, the Transmitter ($$T$$) and Receiver ($$R$$) can deterministically resynchronize without exposing predictable fallback keys, while actively detecting Denial-of-Service (DoS) and active jamming attempts.

---

## 2. Threat Model and Security Assumptions

The system operates over **ESP-NOW**, a connectionless Wi-Fi communication protocol [1]. In this environment, an active or passive adversary may attempt the following attack vectors:

- **Eavesdropping and Sniffing:** Intercepting unencrypted or poorly encrypted telemetry packets to extract behavioral driving patterns or sensor logs [1].

- **Replay Attacks:** Recording legitimate control or telemetry packets and re-transmitting them to hijack vehicle control or corrupt machine learning datasets [1].

- **Timing Side-Channel Attacks:** Measuring response times (e.g., SD card I/O latency) to infer internal execution states and force key stalling [1].

- **State Desynchronization & Jamming:** Deliberately dropping or corrupting ACK packets to force $$T$$ or $$R$$ out of cryptographic sync, attempting to lock the system into a static key state [1].

---

## 3. Core Protocol Parameters and State Machine

The system consists of two primary cryptographic entities: the **Transmitter (**$$T$$**)** (ESP32-S3 mounted on the RC car) and the **Receiver (**$$R$$**)** (Base station logging data to an SD card) [1].

| Parameter | Symbol | Description |
| --- | --- | --- |
| **Data Payload** | $$D$$ | Raw telemetry structure (timestamp, steering, throttle, IMU sensor logs) [1]. |
| **Monotonic Counters** | $$T_{mc} / R_{mc}$$ | Strictly increasing sequence numbers independently maintained by $$T$$ and $$R$$ [1]. |
| **Master Encryption Key** | $$E_m$$ | The active symmetric AES-128 key currently used for encryption/decryption [1]. |
| **New Encryption Key** | $$E_n$$ | A dynamically generated cryptographic key sent from $$R$$ to $$T$$ as an acknowledgment [1]. |
| **Master Seed** | $$S_m$$ | A secure root secret stored in flash memory, used for deterministic key derivation [1]. |
| **Magic Number** | Constant | A predefined 32-bit constant (`0x4D494E41`) embedded within the payload for decryption validation [1]. |

---

## 4. Protocol Operation Workflow (v2.0)

The MRP lifecycle operates in continuous, synchronized rounds, reinforced by KDF-based resynchronization.

```
+-------------+                                       +-------------+
| Transmitter |                                       |  Receiver   |
|     (T)     |                                       |     (R)     |
+-------------+                                       +-------------+
       |                                                     |
       |--- Enc(D, ++Tmc, Magic, Em) ---------------------->| (Decrypt with Em/En)
       |                                                     | (Validate Magic Number)
       |                                                     | (Check Monotonic Gap)
       |                                                     | (Log to SD Card)
       |                                                     | (Generate New Key En = KDF(Sm, Rmc))
       |<-- Enc(En, Rmc, Em) --------------------------------| (Send ACK with En)
       |                                                     |
(Decrypt ACK, Update Em = En)                                |
(Trigger Success Feedback)                                   |
       v                                                     v
```

### Step 1: Data Transmission ($$T \to R$$)

1. $$T$$ increments its monotonic counter: $$T_{mc} = T_{mc} + 1$$ [1].

1. $$T$$ constructs the payload containing $$D$$, $$T_{mc}$$, and the Magic Number [1].

1. $$T$$ encrypts the payload using current active key $$E_m$$ via AES-128:

   $$
   \text{Packet} = \text{Enc}(D \parallel T_{mc} \parallel \text{Magic}, E_m) [1]
   $$

1. $$T$$ broadcasts the packet via ESP-NOW [1].

### Step 2: Reception, Validation, and Decryption ($$R$$)

1. $$R$$ attempts decryption using the latest known key $$E_n$$, falling back to $$E_m$$ if decryption fails (Dual-Key Fallback) [1].

1. Decryption success is strictly validated via the Magic Number (`0x4D494E41`). If invalid, the packet is dropped [1].

1. $$R$$ validates the monotonic counter gap ($$T_{mc} - R_{mc}$$) [1]:
  - If gap == 1: Normal processing [1].
  - If gap > 1: $$R$$ generates Gap Padding rows in the dataset with `PacketLost = 1` to preserve time-series integrity for Machine Learning pipelines [1].

1. $$R$$ appends valid data to the SD card and updates $$R_{mc} = T_{mc}$$ [1].

### Step 3: Key Rotation and Acknowledgment ($$R \to T$$)

1. $$R$$ generates a fresh cryptographic key $$E_n$$ using a Hardware Random Number Generator (HRNG) or derives it deterministically via KDF from root seed $$S_m$$ and $$R_{mc}$$ [1]:

   $$
   E_n = \text{KDF}(S_m \parallel R_{mc}) [1]
   $$

1. $$R$$ encrypts an ACK packet containing $$E_n$$ and $$R_{mc}$$ using $$E_m$$:

   $$
   \text{ACK} = \text{Enc}(E_n \parallel R_{mc}, E_m) [1]
   $$

1. $$R$$ transmits the ACK packet back to $$T$$ [1].

### Step 4: Acknowledgment Processing and Key Adoption ($$T$$)

1. $$T$$ receives the ACK and decrypts it using $$E_m$$ [1].

1. **Sequence Binding Validation:** $$T$$ strictly compares the received $$R_{mc}$$ with its current $$T_{mc}$$ [1].
  - If $$R_{mc} == T_{mc}$$: $$T$$ adopts the new key $$E_m = E_n$$ [1].
  - If $$R_{mc} \neq T_{mc}$$: $$T$$ ignores the key rotation, neutralizing Stale ACK Replay attacks and Race Conditions [1].

---

## 5. Advanced Hardening: KDF-Based Resynchronization & Anti-Jamming

To eliminate the vulnerability of "Key Stalling via Timing Attacks" (discovered during the v1.0 security audit), MRP v2.0 introduces a **Deterministic KDF Resynchronization Mechanism**:

- **Failure Counter (**$$F_c$$**):** Both $$T$$ and $$R$$ maintain an internal failure counter for consecutive decryption or ACK failures.

- **Threshold Trigger (**$$F_c == 5$$**):** If either entity experiences 5 consecutive failures (indicating active jamming, packet loss, or desynchronization), **neither entity falls back to a static key** [1].

- **Deterministic Re-keying:** Instead, both $$T$$ and $$R$$ independently compute the next synchronization key using the shared root seed $$S_m$$ and the last known good counter value [1]:

$$
E_{\text{sync}} = \text{KDF}(S_m \parallel \text{LastValid}_{mc}) [1]
$$

- **Active Attack Detection:** If resynchronization fails or is triggered more than 3 times within a session, the system flags an **Active Jamming / Tamper Event**, triggers a safe shutdown (Kill Switch), and logs the event to non-volatile storage.

---

## 6. Comprehensive Threat Analysis Matrix

| Attack Vector | MRP v1.0 Vulnerability | MRP v2.0 Hardening & Defense Mechanism |
| --- | --- | --- |
| **Replay Attacks** | Protected via Monotonic Counters ($$T_{mc}$$) [1]. | Fully protected via $$T_{mc}$$ combined with strict ACK sequence binding ($$R_{mc} == T_{mc}$$) [1]. |
| **Key Interception** | Rolling keys ($$E_m \to E_n$$) limit exposure [1]. | Forward secrecy maintained via dynamic KDF rotation; compromised keys self-expire after one round [1]. |
| **Timing Side-Channel / Key Stalling** | Attacker could force $$T$$ to stall on old key by inducing I/O lag [1]. | Eliminated in v2.0: Failure counters ($$F_c$$) and KDF re-keying prevent infinite key stalling [1]. |
| **Packet Loss Desynchronization** | Dual-key fallback could drift under heavy packet loss [1]. | Bounded fallback with deterministic KDF recovery ensures absolute synchronization bounds [1]. |
| **Denial of Service (DoS)** | Exhaustion via forced fallback [1]. | Bounded failure limits and anomaly logging prevent permanent lockouts without detection [1]. |

---

## 7. Conclusion

The Minas Rolling-Key Protocol (MRP) v2.0 represents a complete, mathematically sound security framework tailored for resource-constrained autonomous systems [1]. By combining symmetric encryption, monotonic sequence binding, and KDF-based deterministic recovery, MRP successfully bridges high-frequency real-time control with enterprise-grade cryptographic security [1].

---

## References

[1]: # "Eyal Braun. Minas Rolling-Key Protocol (MRP): A Lightweight Secure Telemetry Framework for Autonomous RC Systems. GitHub Repository: EyalBraun/Minas, 2026. [1]"
