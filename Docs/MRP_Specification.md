# Minas Rolling-Key Protocol (MRP) v1.1 — Research Prototype

## Scope

MRP is the original application-layer telemetry protocol designed for the Minas RC-car research platform. It defines the telemetry structure, sequence-counter workflow, receiver logging behavior, and acknowledgement message used by MinasDT and MinasDR.

AES-128 and SHA-256 are established cryptographic primitives used as building blocks; they are not original algorithms created by Minas. The current protocol remains a research prototype and must not be described as production-grade authorization security.

## Message flow

```text
MinasDT -- padded encrypted telemetry --> MinasDR
MinasDR -- padded encrypted ACK -------> MinasDT
```

The transmitter increments a monotonic sequence counter for each telemetry record. The receiver validates the packet, logs it, derives the next key, and sends an acknowledgement containing the accepted counter and next derived key.

## Telemetry fields

The telemetry payload contains sequence number, transmitter uptime timestamp, throttle command, steering command, sonar distance, packet-loss field, owner/non-owner experiment label, elapsed time, signed steering rate, signed throttle rate, and a magic value for prototype validation.

The owner label is a research ground-truth label selected for a trial. It is not cryptographic proof of who is holding the controller.

## Trial files

The receiver creates a new file when a valid received label changes. Each file contains one fixed label and should represent one driver/trial interval. Missing packets are recorded through the loss count on the next received row; no artificial zero-valued behavioral rows are created.

## Packet lengths

Payloads are encrypted using 16-byte AES blocks. The sender must transmit the padded ciphertext length, not `sizeof(payload)` when that value is not block-aligned. The receiver rejects input lengths that are zero, larger than the internal buffer, or not divisible by 16.

## Current security limitations

The prototype uses AES-ECB and a fixed magic value. The magic value is not a message-authentication code. The master seed is compiled into both firmware images. Replay resistance, sender authentication, secure provisioning, and robust counter resynchronization are not complete. These limitations must be stated in any project report.

Before classifier output is allowed to control the vehicle, the protocol should be redesigned around authenticated encryption with a unique nonce, authenticated associated data, sender identity, session identifier, freshness validation, and a locally fail-safe stop policy.

## Original project contribution

The Minas contribution is the integration and application-layer design: telemetry fields, trial labeling, sequence/gap handling, receiver logging, rolling-key message flow, and the planned driver-authorization interface. The cryptographic primitives and wireless transport are established technologies used by the project.
