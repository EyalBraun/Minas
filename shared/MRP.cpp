#include "MRP.h"
#include <string.h>

MRPProtocol::MRPProtocol() {
    mbedtls_aes_init(&_aesCtx);
}

MRPProtocol::~MRPProtocol() {
    mbedtls_aes_free(&_aesCtx);
}

bool MRPProtocol::encrypt(const uint8_t* input, size_t inputLen, uint8_t* output,
                          const uint8_t* key, size_t& outputLen) {
    outputLen = 0;
    if (input == nullptr || output == nullptr || key == nullptr ||
        inputLen == 0 || inputLen > MRP_MAX_CIPHERTEXT_SIZE) {
        return false;
    }

    const size_t paddedLen = mrpPaddedLength(inputLen);
    if (paddedLen > MRP_MAX_CIPHERTEXT_SIZE) return false;

    uint8_t paddedInput[MRP_MAX_CIPHERTEXT_SIZE] = {};
    memcpy(paddedInput, input, inputLen);

    if (mbedtls_aes_setkey_enc(&_aesCtx, key, 128) != 0) return false;
    for (size_t offset = 0; offset < paddedLen; offset += 16U) {
        if (mbedtls_aes_crypt_ecb(&_aesCtx, MBEDTLS_AES_ENCRYPT,
                                  paddedInput + offset, output + offset) != 0) {
            memset(output, 0, MRP_MAX_CIPHERTEXT_SIZE);
            return false;
        }
    }
    outputLen = paddedLen;
    return true;
}

bool MRPProtocol::decrypt(const uint8_t* input, size_t inputLen, uint8_t* output,
                          const uint8_t* key) {
    if (input == nullptr || output == nullptr || key == nullptr ||
        inputLen == 0 || inputLen > MRP_MAX_CIPHERTEXT_SIZE ||
        (inputLen % 16U) != 0) {
        return false;
    }

    if (mbedtls_aes_setkey_dec(&_aesCtx, key, 128) != 0) return false;
    uint8_t plaintext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    for (size_t offset = 0; offset < inputLen; offset += 16U) {
        if (mbedtls_aes_crypt_ecb(&_aesCtx, MBEDTLS_AES_DECRYPT,
                                  input + offset, plaintext + offset) != 0) {
            return false;
        }
    }
    memcpy(output, plaintext, inputLen);

    // The caller validates the expected structure and its magic field. The
    // magic is before zero padding, so it cannot be checked at ciphertext end.
    return true;
}

void MRPProtocol::deriveKey(const uint8_t* seed, uint32_t counter,
                            uint8_t* keyOut) {
    if (seed == nullptr || keyOut == nullptr) return;

    uint8_t input[MASTER_SEED_SIZE + sizeof(uint32_t)] = {};
    uint8_t hash[32] = {};
    memcpy(input, seed, MASTER_SEED_SIZE);
    memcpy(input + MASTER_SEED_SIZE, &counter, sizeof(counter));
    if (mbedtls_sha256_ret(input, sizeof(input), hash, 0) != 0) {
        memset(keyOut, 0, AES_KEY_SIZE);
        return;
    }
    memcpy(keyOut, hash, AES_KEY_SIZE);
}
