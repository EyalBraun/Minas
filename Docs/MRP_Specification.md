\# Minas Rolling-Key Protocol (MRP) v1.0



\## Purpose and current scope



MRP is the shared transport prototype used by MinasDT and MinasDR to protect telemetry exchanged over ESP-NOW and to keep the two devices synchronized after successful packets. This document describes the implementation currently present in the repository. It does not claim that driver classification or an active authorization decision is implemented.



The current protocol provides confidentiality-like AES block encryption, counter-based key derivation, a basic magic-number validation check and a key-synchronization ACK. It is a research prototype and is not a production authorization protocol.



\## Nodes and message flow



```

MinasDT (car)                         MinasDR (base station)

&#x20;     |                                        |

&#x20;     | encrypted telemetry\_payload\_t         |

&#x20;     |--------------------------------------->|

&#x20;     |                                        | decrypt and validate magic

&#x20;     |                                        | detect sequence gaps

&#x20;     |                                        | log and derive next key

&#x20;     | encrypted ack\_payload\_t                |

&#x20;     |<---------------------------------------|

&#x20;     | validate receiverCounter              |

&#x20;     | adopt newKey                           |

```



Telemetry is scheduled by MinasDT at the configured interval. MinasDR processes a valid packet, writes the record and returns an ACK containing the last accepted counter and the next derived key.



\## Key derivation



Both devices contain the same 32-byte pre-shared `masterSeed` in the current prototype. For counter `c`, the implementation constructs:



```

K\_c = first\_16\_bytes(SHA-256(masterSeed || encoding(c)))

```



MinasDT starts with the key derived for counter 0. MinasDR derives the next key after accepting a telemetry packet. The transmitter adopts the new key only when the ACK counter equals its current transmission counter.



The current seed is compiled into firmware for demonstration purposes. A future version must define secure provisioning and key rotation that do not rely on a publicly recoverable firmware constant.



\## Payloads



\### Telemetry payload



`telemetry\_payload\_t` contains the sequence number, timestamp, throttle, steering, sonar distance, packet-loss flag, the local Owner/Guest experiment label, elapsed time, steering velocity, throttle velocity and `MAGIC\_NUMBER`.



The `isOwner` field is currently a locally selected experiment label. It is not a station-side classification result and does not authenticate the person operating the controller.



\### Key-synchronization ACK



`ack\_payload\_t` contains:



| Field | Meaning |

| --- | --- |

| `receiverCounter` | Last telemetry sequence accepted by the receiver. |

| `newKey` | Key derived for the next counter. |

| `magic` | Basic message-type/decryption validation value. |



The ACK currently does not contain a driver-authorization result or a `CONTINUE`/`STOP` command.



\### Reserved future authorization payload



`authorization\_decision\_payload\_t` is defined in `shared/MRP.h` as a reserved interface for a later research stage. It contains a telemetry counter, an authorization bit, a drive decision and a confidence value. It is intentionally not sent or interpreted in the current firmware.



\## Receiver processing



After successful decryption, MinasDR checks the sequence number. If one or more sequence numbers are missing, it writes padded records with `packetLost = 1`. It then derives additional logging fields and appends the received data to `/minas\_master\_dataset.csv` on the SD card.



The receiver tries the next-key candidate and current key to tolerate a dropped ACK. After accepting a packet, it derives the next key and encrypts an ACK for the sender.



\## Failure and safety behaviour



A failed decryption increments a failure counter. After the configured threshold, the prototype deterministically derives keys from the last valid counter in an attempt to resynchronise.



The car has local safety behaviour for controller disconnection and nearby sonar obstacles. An ACK timeout or a future unauthorized-driver result does not currently stop the car. When an authorization stage is implemented, the fail-safe policy should be explicit: invalid, stale, unauthenticated or missing decisions should default to stopping the drive output.



\## Security limitations



The current implementation has important limitations:



1\. The master seed is present in both firmware images.



1\. AES-ECB is used block-by-block without an authenticated-encryption mode.



1\. The magic number is not a cryptographic message-authentication code.



1\. The counter and key transition logic has not yet been evaluated against replay, reordering, packet injection or active interception.



1\. The receiver-side authorization classifier and authenticated decision channel are not implemented.



These limitations are recorded deliberately so that future experiments can define a threat model and measure improvements rather than overstating the current security level.



\## Reproducibility checklist



Before using the protocol in an experiment, record the firmware revision, board types, ESP-NOW peer configuration, telemetry interval, seed-provisioning method, packet-loss conditions, counter traces and receiver logs. A protocol result should be reproducible from the source revision and the recorded test conditions.

