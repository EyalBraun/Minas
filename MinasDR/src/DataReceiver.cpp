/**
 * ============================================================================
 * Project: MinasDR (Data Receiver) - MRP v1.0 Hardened with ML Logging
 * File: DataReceiver.cpp
 * Description: Base station with KDF-based rotation and ML feature CSV logging.
 * ============================================================================
 */

#include "../include/DataReceiver.h"
#include "../../shared/MRP.h"

MRPProtocol mrp;

// --- Security State Variables ---
static uint8_t em[AES_KEY_SIZE];
static uint8_t en[AES_KEY_SIZE];
static unsigned long lastValidMc = 0;
static int failureCounter = 0;

static uint8_t masterSeed[MASTER_SEED_SIZE] = {
    0x4D, 0x49, 0x4E, 0x41, 0x5F, 0x53, 0x45, 0x43,
    0x52, 0x45, 0x54, 0x5F, 0x53, 0x45, 0x45, 0x44,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

DataReceiver::DataReceiver()
    : _initialized(false), _lastReceivedSeq(0), _totalLostPackets(0) {
    memset(&_lastData, 0, sizeof(_lastData));
    mrp.deriveKey(masterSeed, 0, em);
    mrp.deriveKey(masterSeed, 1, en);
}

bool DataReceiver::begin() {
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
        return false;
    }
    _initialized = true;
    checkAndCreateHeader();
    WiFi.mode(WIFI_STA);
    esp_now_init();
    return true;
}

void DataReceiver::checkAndCreateHeader() {
    if (!SD_MMC.exists(MASTER_LOG_FILE)) {
        File file = SD_MMC.open(MASTER_LOG_FILE, FILE_WRITE);
        if (file) {
            // Updated CSV Header with new ML Features
            file.println("Sequence,Timestamp,Throttle,Steering,Sonar,PacketLost,IsOwner,DeltaTime,SteerVel,ThrotVel,SteerJerk,ThrotJerk,STCorr");
            file.close();
        }
    }
}

void DataReceiver::logToCSV(telemetry_payload_t data) {
    if (!_initialized) return;
    File file = SD_MMC.open(MASTER_LOG_FILE, FILE_APPEND);
    if (!file) return;

    // Derived Features
    int steeringJerk = (_lastReceivedSeq == 0) ? 0 : abs(data.steering - _lastData.steering);
    int throttleJerk = (_lastReceivedSeq == 0) ? 0 : abs(data.throttle - _lastData.throttle);
    int stCorrelation = data.steering * data.throttle;

    // Logging all features including the new real-time ML metrics
    file.printf("%lu,%lu,%d,%d,%d,%d,%d,%lu,%.4f,%.4f,%d,%d,%d\n",
        data.sequenceNumber, data.timestamp, data.throttle,
        data.steering, data.sonarDistance, data.packetLost, data.isOwner,
        data.deltaTime, data.steerVelocity, data.throttleVelocity,
        steeringJerk, throttleJerk, stCorrelation);

    file.close();
    _lastData = data;
}

void DataReceiver::handleIncomingData(const uint8_t* mac, const uint8_t* data, int len) {
    uint8_t decryptedBuffer[64] = { 0 };
    bool decrypted = false;

    if (failureCounter >= FAILURE_THRESHOLD) {
        mrp.deriveKey(masterSeed, lastValidMc, em);
        mrp.deriveKey(masterSeed, lastValidMc + 1, en);
        failureCounter = 0;
    }

    if (mrp.decrypt(data, len, decryptedBuffer, en)) {
        decrypted = true;
        memcpy(em, en, AES_KEY_SIZE);
        failureCounter = 0;
    }
    else if (mrp.decrypt(data, len, decryptedBuffer, em)) {
        decrypted = true;
        failureCounter = 0;
    }
    else {
        failureCounter++;
        return;
    }

    telemetry_payload_t incomingData;
    memcpy(&incomingData, decryptedBuffer, sizeof(incomingData));

    if (_lastReceivedSeq != 0 && incomingData.sequenceNumber > _lastReceivedSeq + 1) {
        for (unsigned long missingSeq = _lastReceivedSeq + 1; missingSeq < incomingData.sequenceNumber; missingSeq++) {
            telemetry_payload_t paddedData;
            memset(&paddedData, 0, sizeof(paddedData));
            paddedData.sequenceNumber = missingSeq;
            paddedData.packetLost = 1;
            paddedData.isOwner = incomingData.isOwner;
            paddedData.magic = MAGIC_NUMBER;
            logToCSV(paddedData);
            _totalLostPackets++;
        }
    }

    incomingData.packetLost = 0;
    logToCSV(incomingData);
    _lastReceivedSeq = incomingData.sequenceNumber;
    lastValidMc = _lastReceivedSeq;

    mrp.deriveKey(masterSeed, _lastReceivedSeq + 1, en);

    ack_payload_t ack;
    ack.receiverCounter = _lastReceivedSeq;
    memcpy(ack.newKey, en, AES_KEY_SIZE);
    ack.magic = MAGIC_NUMBER;

    uint8_t ackCipher[64] = { 0 };
    mrp.encrypt((uint8_t*)&ack, sizeof(ack), ackCipher, em);
    esp_now_send(mac, ackCipher, sizeof(ack));
}
s