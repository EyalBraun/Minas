/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: DataReceiver.cpp
 * Description: Implementation of telemetry reception using SD_MMC library.
 * ============================================================================
 */

#include "../include/DataReceiver.h"

DataReceiver::DataReceiver()
    : _initialized(false), _lastReceivedSeq(0), _totalLostPackets(0) {
    memset(&_lastData, 0, sizeof(_lastData));
}

bool DataReceiver::begin() {
    // 1. Initialize SD_MMC with your specific pins
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

    // Using 1-bit mode (true, true) as in your original code
    if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
        Serial.println("[ERROR] SD_MMC Mount Failed! Check wiring.");
        return false;
    }

    _initialized = true;
    checkAndCreateHeader();

    // 2. Initialize WiFi & ESP-NOW
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] Error initializing ESP-NOW");
        return false;
    }

    Serial.println("[INFO] MinasDR Initialized Successfully (SD_MMC Mode).");
    return true;
}

void DataReceiver::checkAndCreateHeader() {
    if (!SD_MMC.exists(MASTER_LOG_FILE)) {
        File file = SD_MMC.open(MASTER_LOG_FILE, FILE_WRITE);
        if (file) {
            file.println("Sequence,Timestamp,Throttle,Steering,Sonar,PacketLost,SteerJerk,ThrottleJerk,STCorr");
            file.close();
            Serial.println("[INFO] Master log file created.");
        }
    }
}

void DataReceiver::logToCSV(struct_message data) {
    if (!_initialized) return;

    File file = SD_MMC.open(MASTER_LOG_FILE, FILE_APPEND);
    if (!file) {
        Serial.println("[ERROR] Failed to open master log file.");
        return;
    }

    // --- ML FEATURE ENGINEERING ---
    int steeringJerk = (_lastReceivedSeq == 0) ? 0 : abs(data.steering - _lastData.steering);
    int throttleJerk = (_lastReceivedSeq == 0) ? 0 : abs(data.throttle - _lastData.throttle);
    int stCorrelation = data.steering * data.throttle;

    file.printf("%lu,%lu,%d,%d,%d,%d,%d,%d,%d\n",
        data.sequenceNumber, data.timestamp, data.throttle,
        data.steering, data.sonarDistance, data.packetLost,
        steeringJerk, throttleJerk, stCorrelation);

    file.close();
    _lastData = data;
}

void DataReceiver::handleIncomingData(const uint8_t* mac, const uint8_t* data, int len) {
    struct_message incomingData;
    memcpy(&incomingData, data, sizeof(incomingData));

    // --- GAP FILLING / PADDING LOGIC ---
    if (_lastReceivedSeq != 0 && incomingData.sequenceNumber > _lastReceivedSeq + 1) {
        unsigned long gap = incomingData.sequenceNumber - (_lastReceivedSeq + 1);
        Serial.printf("[WARNING] Gap detected! Missed %lu packets. Padding...\n", gap);

        for (unsigned long missingSeq = _lastReceivedSeq + 1; missingSeq < incomingData.sequenceNumber; missingSeq++) {
            struct_message paddedData;
            paddedData.sequenceNumber = missingSeq;
            paddedData.timestamp = 0;
            paddedData.throttle = 0;
            paddedData.steering = 0;
            paddedData.sonarDistance = 0;
            paddedData.packetLost = 1;

            logToCSV(paddedData);
            _totalLostPackets++;
        }
    }

    incomingData.packetLost = 0;
    logToCSV(incomingData);

    _lastReceivedSeq = incomingData.sequenceNumber;

    Serial.printf("[LOG] Seq: #%lu | Thr: %d | Str: %d | Total Lost: %d\n",
        incomingData.sequenceNumber, incomingData.throttle, incomingData.steering, _totalLostPackets);
}
