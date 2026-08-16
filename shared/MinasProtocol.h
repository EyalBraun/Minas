#ifndef MINAS_PROTOCOL_H
#define MINAS_PROTOCOL_H

#include <Arduino.h>
#include <stdint.h>

// Transport-level research protocol. Add authenticated encryption before use
// in a safety-critical or production system.
static constexpr uint32_t MINAS_PROTOCOL_MAGIC = 0x4D494E41UL; // "MINA"
static constexpr uint8_t MINAS_PROTOCOL_VERSION = 2;
static constexpr uint32_t MINAS_COMMAND_TTL_MS = 180;

enum MinasMessageType : uint8_t {
    MINAS_MSG_AUTHORIZED_COMMAND = 1,
    MINAS_MSG_VEHICLE_TELEMETRY = 2
};

enum MinasDecision : uint8_t {
    MINAS_DECISION_STOP = 0,
    MINAS_DECISION_ALLOW_OWNER = 1,
    MINAS_DECISION_UNKNOWN = 2
};

// DT -> DR. This is the only packet that may cause movement.
struct __attribute__((packed)) AuthorizedVehicleCommand {
    uint32_t magic;
    uint8_t version;
    uint8_t messageType;
    uint8_t decision;
    uint8_t allowMotion;
    uint32_t sequence;
    uint32_t inputSequence;
    uint32_t issuedAtMs;
    uint32_t expiresAtMs;
    int16_t steering;
    int16_t throttle;
    uint16_t confidencePermille;
    uint8_t reserved[6];
    uint32_t crc32;
};

// DR -> DT. The S3 may add sensors later without changing the command path.
struct __attribute__((packed)) VehicleTelemetry {
    uint32_t magic;
    uint8_t version;
    uint8_t messageType;
    uint16_t reserved0;
    uint32_t sequence;
    uint32_t timestampMs;
    int16_t steeringApplied;
    int16_t throttleApplied;
    int16_t sonarDistanceCm;
    uint16_t packetLoss;
    uint8_t failsafeActive;
    uint8_t reserved[5];
    uint32_t crc32;
};

inline uint32_t minasCrc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1U)));
        }
    }
    return ~crc;
}

template <typename T>
inline void minasFinalize(T& packet) {
    packet.crc32 = 0;
    packet.crc32 = minasCrc32(reinterpret_cast<const uint8_t*>(&packet), sizeof(T));
}

template <typename T>
inline bool minasValidate(const T& packet) {
    if (packet.magic != MINAS_PROTOCOL_MAGIC ||
        packet.version != MINAS_PROTOCOL_VERSION) {
        return false;
    }
    T copy = packet;
    const uint32_t expected = copy.crc32;
    copy.crc32 = 0;
    return expected == minasCrc32(reinterpret_cast<const uint8_t*>(&copy), sizeof(T));
}

#endif // MINAS_PROTOCOL_H
