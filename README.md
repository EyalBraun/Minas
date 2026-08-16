# Minas: Secure RC-Car Telemetry and Driver-Authorization Research Platform

**Author:** Eyal Braun**Platforms:** ESP32-S3 car controller and ESP32 base station**Framework:** Arduino / PlatformIO**Current status:** Telemetry, encrypted transport, receiver logging and key-rotation prototype. Driver classification and active authorization decisions are not implemented yet.

## Project question

Minas is being developed to investigate whether a driver's authorization can eventually be inferred from driving-behaviour features such as steering, throttle and their rates of change, and whether that decision can be delivered securely to an embedded vehicle controller in real time.

The current repository implements the data-collection and secure-transport foundation. It does **not** yet claim to classify drivers or to return an active `CONTINUE`/`STOP` command. Those are future research stages and are documented as such.

## Current architecture

The system has two embedded nodes:

| Component | Role | Current responsibilities |
| --- | --- | --- |
| `MinasDT` | Transmitter on the car | Reads PS5 input, controls steering and ESC, measures sonar distance, builds telemetry and encrypts it before ESP-NOW transmission. |
| `MinasDR` | Receiver/base station | Receives and decrypts telemetry, detects sequence gaps, derives logging features, writes the dataset to an SD card and returns an encrypted key-synchronization ACK. |
| `shared` | Shared protocol library | AES-128 transport prototype, SHA-256 counter-based key derivation, payload definitions and protocol constants. |

The current wire flow is:

```
MinasDT -- encrypted telemetry --> MinasDR
MinasDR -- encrypted ACK + next key --> MinasDT
```

The future authorization-decision payload is defined in `shared/MRP.h` as a reserved data structure, but it is intentionally not transmitted or interpreted until a validated classifier and an evaluation protocol exist.

## Telemetry collected

Each telemetry record contains a monotonic sequence number, timestamp, throttle, steering, sonar distance, packet-loss flag, the local Owner/Guest experiment label, elapsed time since the previous packet, steering velocity, throttle velocity and a magic value used for basic payload validation.

The `isOwner` field is currently an experiment label selected locally on the car with the PS5 controller. It is **not** the output of a station-side classifier and must not be interpreted as automatically verified identity.

The receiver derives additional logging fields, including steering change, throttle change and a simple steering-throttle product. These fields support future dataset exploration; they are not yet a validated driver-recognition model.

## Safety behaviour currently implemented

The car locally returns the ESC to neutral when the PS5 controller disconnects and blocks forward throttle when the sonar reports an obstacle within the configured threshold. These local fail-safes are separate from the future station-side authorization decision. The receiver currently does not command the car to stop because of driver classification or an ACK timeout.

## Repository structure

```
Minas/
├── MinasDT/                         Car transmitter firmware
├── MinasDR/                         Base-station receiver firmware
├── shared/                          Shared MRP definitions and implementation
├── Docs/                            Protocol specification, wiring diagram and documentation
├── README.md                        Project overview and current scope
└── platformio.ini                   Platform-specific build configuration in each firmware directory
```

## Documentation

- [MRP technical specification](Docs/MRP_Specification.md)

- [Documentation index](Docs/README.md)

- [Complete wiring diagram](Docs/Minas_Wiring_Complete.png)

The MRP specification describes the implementation as it exists in this repository, including its current limitations. It does not present the future classifier or authorization command as completed functionality.

## Research roadmap

1. Validate the telemetry pipeline and collect labelled driving sessions under controlled conditions.

1. Define a reproducible train/validation/test split that prevents data leakage between sessions.

1. Compare baseline and candidate driver-recognition methods using accuracy, precision, recall, F1, false-accept rate and false-reject rate.

1. Add an authenticated authorization-decision message only after the classifier and failure policy are specified.

1. Test latency, packet loss, replay resistance, malformed messages and safe behaviour on timeout or uncertainty.

1. Document limitations, failure cases and reproducibility instructions.

## Scope and security disclaimer

The current MRP implementation is an embedded transport prototype for the research platform. It should not be treated as a production-grade authorization protocol. In particular, the implementation currently uses a pre-shared seed in firmware, AES-ECB block encryption and a magic-number check rather than a complete authenticated-encryption design. These limitations are part of the engineering work that must be addressed before making security claims beyond the prototype.