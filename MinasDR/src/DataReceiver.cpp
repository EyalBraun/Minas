/**
 * ============================================================================
 * Project: MinasDR (Data Receiver) - MRP Secure Version
 * File: DataReceiver.cpp
 * Description: Telemetry receiver with MRP decryption and isOwner logging.
 * ============================================================================
 */

#include "../include/DataReceiver.h"
#include "../../shared/MRP.h"

MRPProtocol mrp; 


static uint8_t em[AES_KEY_SIZE] = { 0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xEF };
static uint8_t en[AES_KEY_SIZE] = { 0 };

DataReceiver::DataReceiver()
    : _initialized(false), _lastReceivedSeq(0), _totalLostPackets(0) {
    memset(&_lastData, 0, sizeof(_lastData));
    memcpy(en, em, AES_KEY_SIZE);
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
            file.println("Sequence,Timestamp,Throttle,Steering,Sonar,PacketLost,IsOwner,SteerJerk,ThrottleJerk,STCorr");
            file.close();
        }
    }
}

void DataReceiver::logToCSV(telemetry_payload_t data) {
    if (!_initialized) return;
    File file = SD_MMC.open(MASTER_LOG_FILE, FILE_APPEND);
    if (!file) return;

    int steeringJerk = (_lastReceivedSeq == 0) ? 0 : abs(data.steering - _lastData.steering);
    int throttleJerk = (_lastReceivedSeq == 0) ? 0 : abs(data.throttle - _lastData.throttle);
    int stCorrelation = data.steering * data.throttle;

    file.printf("%lu,%lu,%d,%d,%d,%d,%d,%d,%d,%d\n",
        data.sequenceNumber, data.timestamp, data.throttle,
        data.steering, data.sonarDistance, data.packetLost, data.isOwner,
        steeringJerk, throttleJerk, stCorrelation);
    file.close();
    _lastData = data;
}

void DataReceiver::handleIncomingData(const uint8_t* mac, const uint8_t* data, int len) {
    uint8_t decryptedBuffer[64] = { 0 };
    bool decrypted = false;

    if (mrp.decrypt(data, len, decryptedBuffer, en)) {
        decrypted = true;
        memcpy(em, en, AES_KEY_SIZE);
    }
    else if (mrp.decrypt(data, len, decryptedBuffer, em)) {
        decrypted = true;
    }

    if (!decrypted) return;

    telemetry_payload_t incomingData;
    memcpy(&incomingData, decryptedBuffer, sizeof(incomingData));

    if (_lastReceivedSeq != 0 && incomingData.sequenceNumber > _lastReceivedSeq + 1) {
        for (unsigned long missingSeq = _lastReceivedSeq + 1; missingSeq < incomingData.sequenceNumber; missingSeq++) {
            telemetry_payload_t paddedData;
            paddedData.sequenceNumber = missingSeq;
            paddedData.timestamp = 0;
            paddedData.throttle = 0;
            paddedData.steering = 0;
            paddedData.sonarDistance = 0;
            paddedData.packetLost = 1;
            paddedData.isOwner = incomingData.isOwner; // Assume mode didn't change mid-loss
            paddedData.magic = MAGIC_NUMBER;
            logToCSV(paddedData);
            _totalLostPackets++;
        }
    }

    incomingData.packetLost = 0;
    logToCSV(incomingData);
    _lastReceivedSeq = incomingData.sequenceNumber;

    mrp.generateNewKey(en);
    ack_payload_t ack;
    ack.receiverCounter = _lastReceivedSeq;
    memcpy(ack.newKey, en, AES_KEY_SIZE);
    ack.magic = MAGIC_NUMBER;
    uint8_t ackCipher[64] = { 0 };
    mrp.encrypt((uint8_t*)&ack, sizeof(ack), ackCipher, em);
    esp_now_send(mac, ackCipher, sizeof(ack));
}
