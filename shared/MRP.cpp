/**
 * ============================================================================
 * Project: Minas - Minas Rolling-Key Protocol (MRP)
 * File: MRP.cpp
 * Description: Implementation of AES-128 encryption and key generation.
 * ============================================================================
 */

#include "MRP.h"

MRPProtocol::MRPProtocol() {
    mbedtls_aes_init(&_aesCtx);
}

void MRPProtocol::encrypt(const uint8_t* input, size_t inputLen, uint8_t* output, const uint8_t* key) {
    mbedtls_aes_setkey_enc(&_aesCtx, key, 128);
    // Simple ECB mode block padding or direct block cipher for telemetry
    // For robust telemetry, we pad to 16-byte blocks
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

    // Check Magic Number in decrypted payload if it matches telemetry struct
    telemetry_payload_t* testPl = (telemetry_payload_t*)output;
    if (testPl->magic == MAGIC_NUMBER) {
        return true;
    }

    ack_payload_t* testAck = (ack_payload_t*)output;
    if (testAck->magic == MAGIC_NUMBER) {
        return true;
    }

    return false; // Decryption failed or incorrect key
}

void MRPProtocol::generateNewKey(uint8_t* keyOut) {
    for (int i = 0; i < AES_KEY_SIZE; i++) {
        keyOut[i] = (uint8_t)(esp_random() % 256);
    }
}
