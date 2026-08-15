/**
 * ============================================================================
 * Project: Minas - Minas Rolling-Key Protocol (MRP) v1.0
 * File: MRP.h
 * Description: Hardened cryptographic definitions with ML feature support.
 * ============================================================================
 */

#ifndef MRP_H
#define MRP_H

#include <Arduino.h>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"

 // --- Protocol Constants ---
#define AES_KEY_SIZE 16          // 128 bits
#define MASTER_SEED_SIZE 32      // 256 bits for KDF root
#define MAGIC_NUMBER 0x4D494E41  // "MINA" in ASCII hex
#define FAILURE_THRESHOLD 5      // Max failures before deterministic resync

// Telemetry Data Payload (48 bytes - AES block aligned)
typedef struct telemetry_payload_t {
    unsigned long sequenceNumber;
    unsigned long timestamp;      // Absolute system time (ms)
    int throttle;
    int steering;
    int sonarDistance;
    int packetLost;
    int isOwner;                  // 1 = Owner, 0 = Guest

    // --- New ML Features ---
    unsigned long deltaTime;      // ms since last packet
    float steerVelocity;          // Rate of steering change
    float throttleVelocity;       // Rate of throttle change

    uint32_t magic;
} telemetry_payload_t;

// Encrypted Wire Packet
typedef struct encrypted_packet_t {
    uint8_t ciphertext[64];       // Buffer for encrypted payload
    size_t length;
} encrypted_packet_t;

// ACK Packet Payload
typedef struct ack_payload_t {
    unsigned long receiverCounter;
    uint8_t newKey[AES_KEY_SIZE];
    uint32_t magic;
} ack_payload_t;

class MRPProtocol {
public:
    MRPProtocol();

    // Core Cryptography
    void encrypt(const uint8_t* input, size_t inputLen, uint8_t* output, const uint8_t* key);
    bool decrypt(const uint8_t* input, size_t inputLen, uint8_t* output, const uint8_t* key);

    // Key Derivation Function (KDF)
    void deriveKey(const uint8_t* seed, unsigned long counter, uint8_t* keyOut);

private:
    mbedtls_aes_context _aesCtx;
};

#endif // MRP_H
