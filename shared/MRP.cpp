/**
 * ============================================================================
 * Project: Minas - Minas Rolling-Key Protocol (MRP) v1.0
 * File: MRP.cpp
 * Description: Implementation of AES-128 and SHA-256 based KDF with ML support.
 * ============================================================================
 */

#include "MRP.h"
#include <string.h>

MRPProtocol::MRPProtocol() {
    mbedtls_aes_init(&_aesCtx);
}

void MRPProtocol::encrypt(const uint8_t* input, size_t inputLen, uint8_t* output, const uint8_t* key) {
    mbedtls_aes_setkey_enc(&_aesCtx, key, 128);

    // Ensure 16-byte block alignment (Max buffer size 64 as per encrypted_packet_t)
    size_t paddedLen = ((inputLen + 15) / 16) * 16;
    uint8_t tempInput[64] = { 0 };
    memcpy(tempInput, input, inputLen);

    for (size_t i = 0; i < paddedLen; i += 16) {
        mbedtls_aes_crypt_ecb(&_aesCtx, MBEDTLS_AES_ENCRYPT, tempInput + i, output + i);
    }
}

bool MRPProtocol::decrypt(const uint8_t* input, size_t inputLen, uint8_t* output, const uint8_t* key) {
    mbedtls_aes_setkey_dec(&_aesCtx, key, 128);
    uint8_t tempOutput[64] = { 0 };

    for (size_t i = 0; i < inputLen; i += 16) {
        mbedtls_aes_crypt_ecb(&_aesCtx, MBEDTLS_AES_DECRYPT, input + i, tempOutput + i);
    }

    memcpy(output, tempOutput, inputLen);

    // Validation: Check if the decrypted payload contains the valid Magic Number
    // Checking both struct types (Telemetry and ACK) based on the magic offset
    telemetry_payload_t* testPl = (telemetry_payload_t*)output;
    if (testPl->magic == MAGIC_NUMBER) {
        return true;
    }

    ack_payload_t* testAck = (ack_payload_t*)output;
    if (testAck->magic == MAGIC_NUMBER) {
        return true;
    }

    return false; // Decryption failed or Magic Number mismatch
}

void MRPProtocol::deriveKey(const uint8_t* seed, unsigned long counter, uint8_t* keyOut) {
    uint8_t hash[32];
    uint8_t input[MASTER_SEED_SIZE + sizeof(unsigned long)];

    memcpy(input, seed, MASTER_SEED_SIZE);
    memcpy(input + MASTER_SEED_SIZE, &counter, sizeof(unsigned long));

    mbedtls_sha256_ret(input, sizeof(input), hash, 0);
    memcpy(keyOut, hash, AES_KEY_SIZE);
}
