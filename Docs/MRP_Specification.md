\# Minas Rolling-Key Protocol (MRP): A Lightweight Secure Telemetry Framework for Autonomous RC Systems



\*\*Author:\*\* Eyal Braun \& Manus AI\*\*Project:\*\* Minas - Autonomous RC Car Telemetry \& Security System\*\*Classification:\*\* Technical Specification \& Research Paper



\---



\## Abstract



In wireless telemetry systems for autonomous vehicles, data integrity, anti-replay protection, and lightweight cryptographic authentication are paramount. Traditional static-key encryption schemes are vulnerable to interception and replay attacks. This paper introduces the \*\*Minas Rolling-Key Protocol (MRP)\*\*, a novel, bi-directional cryptographic handshake optimized for resource-constrained microcontrollers (ESP32). MRP ensures forward secrecy by dynamically rolling encryption keys upon every successful packet transmission, while incorporating a robust monotonic counter mechanism and dual-key fallback to handle packet loss gracefully.



\---



\## 1. Introduction and Threat Model



Autonomous RC platforms like \*\*Minas\*\* transmit high-frequency telemetry data (steering angles, throttle inputs, sensor logs) from the vehicle (Transmitter, \*\*T\*\*) to a base station (Receiver, \*\*R\*\*) via ESP-NOW \[1]. In an unencrypted environment, malicious actors can sniff telemetry data or inject spoofed control commands.



\### \*\*The Threat Model:\*\*



\- \*\*Eavesdropping:\*\* Intercepting driving logs to extract behavioral patterns.



\- \*\*Replay Attacks:\*\* Recording legitimate control packets and re-transmitting them to hijack the vehicle.



\- \*\*Packet Loss Environment:\*\* Wireless channels are prone to interference, causing dropped packets that can break cryptographic synchronization.



MRP solves these challenges by combining \*\*symmetric AES encryption\*\*, \*\*dynamic key rotation (Rolling Keys)\*\*, \*\*monotonic sequence counters\*\*, and \*\*magic number validation\*\* for key verification.



\---



\## 2. Protocol Architecture and Terminology



The system consists of two primary cryptographic entities:



1\. \*\*Transmitter (\*\*$$T$$\*\*):\*\* The ESP32-S3 microcontroller mounted on the Minas RC car.



1\. \*\*Receiver (\*\*$$R$$\*\*):\*\* The base station ESP32 responsible for logging data to an SD card.



\### \*\*Core Parameters:\*\*



\- $$D$$\*\* (Data Payload):\*\* The raw telemetry structure containing timestamp, throttle, steering, and sensor data.



\- $$T\_{mc}$$\*\* / \*\*$$R\_{mc}$$\*\* (Monotonic Counters):\*\* Strictly increasing sequence numbers maintained independently by $$T$$ and $$R$$ to prevent packet reordering and replay attacks.



\- $$E\_m$$\*\* (Master/Current Encryption Key):\*\* The active symmetric AES key shared between $$T$$ and $$R$$.



\- $$E\_n$$\*\* (New Encryption Key):\*\* A dynamically generated cryptographic key sent from $$R$$ to $$T$$ as an acknowledgment.



\- \*\*Magic Number:\*\* A predefined 32-bit constant (`0x4D494E41`) embedded within the encrypted payload to verify successful decryption.



\---



\## 3. Protocol Operation Workflow



The MRP lifecycle operates in continuous, synchronized rounds between $$T$$ and $$R$$.



```

+-------------+                         +-------------+

|  Transmitter|                         |  Receiver   |

|     (T)     |                         |     (R)     |

+-------------+                         +-------------+

&#x20;      |                                       |

&#x20;      |--- Enc(D, ++Tmc, Em) -------------->| (Decrypt with Em/En)

&#x20;      |                                       | (Validate Magic Number)

&#x20;      |                                       | (Check Monotonic Gap \& Padding)

&#x20;      |                                       | (Log to SD Card)

&#x20;      |                                       | (Generate New Key En)

&#x20;      |<-- Enc(En, Rmc, Em) ------------------| (Send ACK with En)

&#x20;      |                                       |

(Decrypt ACK, Update Em = En)                  |

(Trigger Success Feedback)                     |

&#x20;      v                                       v

```



\### \*\*Step 1: Data Transmission (\*\*$$T \\rightarrow R$$\*\*)\*\*



1\. $$T$$ increments its monotonic counter: $$T\_{mc} = T\_{mc} + 1$$.



1\. $$T$$ constructs the payload containing $$D$$, $$T\_{mc}$$, and the Magic Number.



1\. $$T$$ encrypts the payload using the current active key $$E\_m$$:



&#x20;  $$

&#x20;  \\text{Packet} = \\text{Enc}(D \\parallel T\_{mc} \\parallel \\text{Magic}, E\_m)

&#x20;  $$



1\. $$T$$ broadcasts the packet via ESP-NOW.



\### \*\*Step 2: Reception, Validation, and Decryption (\*\*$$R$$\*\*)\*\*



1\. Upon receiving the packet, $$R$$ attempts decryption. To handle potential out-of-sync key updates, $$R$$ implements a \*\*Dual-Key Fallback Mechanism\*\*:

&#x20; - First, $$R$$ attempts decryption using the latest known key $$E\_n$$.

&#x20; - If decryption fails (verified by an invalid Magic Number), $$R$$ falls back to the previous key $$E\_m$$.



1\. Once decrypted successfully, $$R$$ validates the monotonic counter gap ($$T\_{mc} - R\_{mc}$$):

&#x20; - If gap == 1: Normal processing.

&#x20; - If gap > 1: $$R$$ generates \*\*Gap Padding rows\*\* in the CSV dataset with `PacketLost = 1` to preserve time-series integrity for Machine Learning models \[2].



1\. $$R$$ appends the valid telemetry data to the master SD card dataset and increments $$R\_{mc} = T\_{mc}$$.



\### \*\*Step 3: Key Rotation and Acknowledgment (\*\*$$R \\rightarrow T$$\*\*)\*\*



1\. $$R$$ generates a fresh cryptographic key $$E\_n$$ using a hardware pseudo-random number generator.



1\. $$R$$ encrypts an acknowledgment packet containing $$E\_n$$ and $$R\_{mc}$$ using the current session key $$E\_m$$:



&#x20;  $$

&#x20;  \\text{ACK} = \\text{Enc}(E\_n \\parallel R\_{mc}, E\_m)

&#x20;  $$



1\. $$R$$ transmits the ACK packet back to $$T$$.



\### \*\*Step 4: Acknowledgment Processing and Key Adoption (\*\*$$T$$\*\*)\*\*



1\. $$T$$ receives the ACK and decrypts it using $$E\_m$$.



1\. \*\*Sequence Validation:\*\* $$T$$ compares the received $$R\_{mc}$$ with its current $$T\_{mc}$$.

&#x20; - If $$R\_{mc} == T\_{mc}$$: $$T$$ adopts the new key: $$E\_m = E\_n$$.

&#x20; - If $$R\_{mc} \\neq T\_{mc}$$: $$T$$ ignores the key rotation, as the ACK corresponds to a stale or out-of-order packet.



1\. $$T$$ triggers an internal success indicator (e.g., visual or auditory feedback) confirming successful round-trip synchronization. If $$T$$ fails to receive or decrypt an ACK, it retains $$E\_m$$ and continues transmission, allowing $$R$$ to gracefully catch up via the dual-key fallback.



\---



\## 4. Security Analysis and Robustness



| Attack Vector | MRP Defense Mechanism |

| --- | --- |

| \*\*Replay Attacks\*\* | Enforced Monotonic Counters ($$T\_{mc}$$); packets with stale or duplicate counters are instantly dropped. |

| \*\*Key Interception\*\* | Rolling Keys ($$E\_m \\rightarrow E\_n$$); compromising a single key exposes only a fraction of the session data. |

| \*\*Packet Loss Desync\*\* | Dual-Key Fallback at the Receiver; $$R$$ retains awareness of the previous key until $$T$$ successfully transitions. |

| \*\*Data Corruption\*\* | Magic Number validation guarantees that decryption failures are detected prior to data ingestion. |



\---



\## 5. Conclusion



The Minas Rolling-Key Protocol (MRP) successfully bridges the gap between high-performance real-time telemetry and cryptographic security in embedded IoT environments. By ensuring forward secrecy through rolling keys while maintaining data integrity for Machine Learning pipelines via monotonic gap padding, MRP provides an enterprise-grade security foundation for the Minas autonomous RC platform.



\---



\## References



\[1]: # "Espressif Systems. ESP-NOW Technical Specification. ESP32 Technical Documentation, 2024."



\[2]: # "Goodfellow, I., Bengio, Y., \& Courville, A. Deep Learning. MIT Press, 2016."

