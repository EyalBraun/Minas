/**
 * Minas Rolling-Key Protocol (MRP) research transport.
 *
 * MRP is the active Controller Unit <-> Vehicle Unit wire protocol.
 * AES-ECB and the fixed research seed are intentionally retained to match
 * the project's specification; this is not production-grade authentication.
 */
#ifndef MINAS_MRP_H
#define MINAS_MRP_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"

#define AES_KEY_SIZE 16U
#define MASTER_SEED_SIZE 32U
#define MRP_MAX_CIPHERTEXT_SIZE 64U
#define MAGIC_NUMBER 0x4D494E41UL
#define MRP_PROTOCOL_VERSION 1U

// Research-only bootstrap seed. Replace with per-pair provisioning before use
// outside the lab; both firmware images must currently contain the same value.
static const uint8_t MRP_MASTER_SEED[MASTER_SEED_SIZE] = {
    0x4D, 0x69, 0x6E, 0x61, 0x73, 0x2D, 0x4D, 0x52,
    0x50, 0x2D, 0x72, 0x65, 0x73, 0x65, 0x61, 0x72,
    0x63, 0x68, 0x2D, 0x73, 0x65, 0x65, 0x64, 0x2D,
    0x76, 0x31, 0x2D, 0x6C, 0x61, 0x62, 0x00, 0x01
};

// DT -> DR. This is the telemetry/command payload defined by MRP.
typedef struct __attribute__((packed)) telemetry_payload_t {
    uint32_t sequenceNumber;
    uint32_t timestamp;
    int32_t throttle;
    int32_t steering;
    int32_t sonarDistance;
    int32_t packetLost;
    int32_t isOwner;
    uint32_t deltaTime;
    float steerVelocity;
    float throttleVelocity;
    uint32_t magic;
} telemetry_payload_t;

// DR -> DT. ACK is encrypted under the current key and carries the next key.
typedef struct __attribute__((packed)) ack_payload_t {
    uint32_t receiverCounter;
    uint8_t newKey[AES_KEY_SIZE];
    uint32_t magic;
} ack_payload_t;

static_assert(sizeof(telemetry_payload_t) == 44, "MRP telemetry must be 44 bytes before padding");
static_assert(sizeof(ack_payload_t) == 24, "MRP ACK must be 24 bytes before padding");

typedef enum drive_decision_t {
    DRIVE_STOP = 0,
    DRIVE_CONTINUE = 1
} drive_decision_t;

inline size_t mrpPaddedLength(size_t plaintextLength) {
    return ((plaintextLength + 15U) / 16U) * 16U;
}

class MRPProtocol {
public:
    MRPProtocol();
    ~MRPProtocol();

    bool encrypt(const uint8_t* input, size_t inputLen, uint8_t* output,
                 const uint8_t* key, size_t& outputLen);
    bool decrypt(const uint8_t* input, size_t inputLen, uint8_t* output,
                 const uint8_t* key);
    void deriveKey(const uint8_t* seed, uint32_t counter, uint8_t* keyOut);

private:
    mbedtls_aes_context _aesCtx;
};

#endif
