/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: DataReceiver.cpp
 * Description: Receives telemetry and creates one CSV file per trial.
 * ============================================================================
 */

#include "../include/DataReceiver.h"
#include "../../shared/MRP.h"
#include <math.h>
#include <string.h>

MRPProtocol mrp;

// -----------------------------------------------------------------------------
// Security state. These variables preserve the current MRP prototype behavior.
// The cryptographic limitations of the original protocol still need to be fixed
// separately before the classifier is allowed to control the vehicle.
// -----------------------------------------------------------------------------
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
    : _initialized(false),
    _hasPreviousData(false),
    _lastReceivedSeq(0),
    _totalLostPackets(0),
    _activeLabel(-1),
    _trialNumber(0),
    _activeFilePath("") {
    memset(&_lastData, 0, sizeof(_lastData));
}

bool DataReceiver::begin() {
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

    if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
        Serial.println("[ERROR] SD_MMC initialization failed");
        return false;
    }

    _initialized = true;
    mrp.deriveKey(masterSeed, 0, em);
    mrp.deriveKey(masterSeed, 1, en);

    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP-NOW initialization failed");
        _initialized = false;
        return false;
    }

    Serial.println("[READY] SD_MMC and ESP-NOW initialized");
    return true;
}

String DataReceiver::makeNextFilePath(uint8_t label) {
    const char* labelText = (label == 1) ? "owner" : "nonowner";
    String path;

    // Use a monotonically increasing trial number and also check the card so
    // that a reboot never overwrites an earlier trial.
    do {
        _trialNumber++;
        path = "/trial_";
        if (_trialNumber < 1000) path += "0";
        if (_trialNumber < 100)  path += "0";
        if (_trialNumber < 10)   path += "0";
        path += String(_trialNumber);
        path += "_";
        path += labelText;
        path += ".csv";
    } while (SD_MMC.exists(path.c_str()));

    return path;
}

bool DataReceiver::writeCsvHeader(uint8_t label,
    const telemetry_payload_t& firstPacket) {
    if (!_activeFile) return false;

    _activeFile.println("# Minas telemetry trial");
    _activeFile.print("# label=");
    _activeFile.println(label == 1 ? "owner" : "non_owner");
    _activeFile.print("# start_sequence=");
    _activeFile.println(firstPacket.sequenceNumber);
    _activeFile.print("# firmware_version=repository_commit_here");
    _activeFile.println();
    _activeFile.println(
        "Sequence,TimestampMs,Throttle,Steering,SonarCm,"
        "PacketLossCountBefore,IsOwner,DeltaTimeMs,SteeringRate,"
        "ThrottleRate,SteeringChange,ThrottleChange,"
        "SteeringThrottleProduct");

    return true;
}

bool DataReceiver::openNewTrialFile(uint8_t label,
    const telemetry_payload_t& firstPacket) {
    closeCurrentTrialFile();

    _activeLabel = static_cast<int>(label);
    _activeFilePath = makeNextFilePath(label);
    _activeFile = SD_MMC.open(_activeFilePath.c_str(), FILE_WRITE);

    if (!_activeFile) {
        Serial.print("[ERROR] Could not open trial file: ");
        Serial.println(_activeFilePath);
        _activeLabel = -1;
        return false;
    }

    if (!writeCsvHeader(label, firstPacket)) {
        closeCurrentTrialFile();
        return false;
    }

    _activeFile.flush();

    Serial.print("[TRIAL] Opened: ");
    Serial.println(_activeFilePath);
    return true;
}

void DataReceiver::closeCurrentTrialFile() {
    if (_activeFile) {
        _activeFile.flush();
        _activeFile.close();
        Serial.print("[TRIAL] Closed: ");
        Serial.println(_activeFilePath);
    }

    _activeFilePath = "";
    _activeLabel = -1;
}

void DataReceiver::logToCurrentTrial(const telemetry_payload_t& data) {
    if (!_activeFile) return;

    int steeringChange = 0;
    int throttleChange = 0;
    int steeringThrottleProduct = data.steering * data.throttle;

    if (_hasPreviousData) {
        steeringChange = data.steering - _lastData.steering;
        throttleChange = data.throttle - _lastData.throttle;
    }

    // packetLost is used here as the number of missing sequence values before
    // this packet. No artificial zero-valued training rows are created.
    _activeFile.printf(
        "%lu,%lu,%d,%d,%d,%d,%d,%lu,%.6f,%.6f,%d,%d,%d\n",
        data.sequenceNumber,
        data.timestamp,
        data.throttle,
        data.steering,
        data.sonarDistance,
        data.packetLost,
        data.isOwner,
        data.deltaTime,
        data.steerVelocity,
        data.throttleVelocity,
        steeringChange,
        throttleChange,
        steeringThrottleProduct);

    // Flush periodically rather than on every row to reduce SD-card overhead.
    if ((data.sequenceNumber % 20) == 0) {
        _activeFile.flush();
    }
}

void DataReceiver::logPacket(const telemetry_payload_t& data,
    bool packetLostBeforeThisPacket) {
    telemetry_payload_t row = data;
    row.packetLost = packetLostBeforeThisPacket ?
        static_cast<int>(data.sequenceNumber - _lastReceivedSeq - 1) : 0;

    logToCurrentTrial(row);
    _lastData = row;
    _lastReceivedSeq = row.sequenceNumber;
    _hasPreviousData = true;
}

void DataReceiver::handleIncomingData(const uint8_t* mac,
    const uint8_t* data,
    int len) {
    if (!_initialized || data == nullptr || mac == nullptr || len <= 0) {
        return;
    }

    uint8_t decryptedBuffer[64] = { 0 };
    bool decrypted = false;

    if (failureCounter >= FAILURE_THRESHOLD) {
        mrp.deriveKey(masterSeed, lastValidMc, em);
        mrp.deriveKey(masterSeed, lastValidMc + 1, en);
        failureCounter = 0;
    }

    // Preserve the existing next-key/current-key compatibility behavior.
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

    if (!decrypted) return;

    telemetry_payload_t incomingData;
    memcpy(&incomingData, decryptedBuffer, sizeof(incomingData));

    // The magic check is performed by MRPProtocol, but verify the label here
    // before using it to select a file.
    if (incomingData.isOwner != 0 && incomingData.isOwner != 1) {
        Serial.println("[DROP] Invalid owner label");
        return;
    }

    // Reject duplicate and old packets. They must not create duplicate ML rows.
    if (_hasPreviousData && incomingData.sequenceNumber <= _lastReceivedSeq) {
        Serial.println("[DROP] Duplicate or out-of-order packet");
        return;
    }

    // A label change starts a completely new trial/file. The first packet of
    // the new file is written only after the new file has been opened.
    if (_activeLabel == -1 || _activeLabel != incomingData.isOwner) {
        if (!openNewTrialFile(static_cast<uint8_t>(incomingData.isOwner),
            incomingData)) {
            return;
        }

        // Do not calculate a feature across two drivers or two trials.
        _hasPreviousData = false;
        memset(&_lastData, 0, sizeof(_lastData));
    }

    bool hasGap = _hasPreviousData &&
        incomingData.sequenceNumber > (_lastReceivedSeq + 1);

    if (hasGap) {
        _totalLostPackets += incomingData.sequenceNumber - _lastReceivedSeq - 1;
    }

    logPacket(incomingData, hasGap);
    lastValidMc = _lastReceivedSeq;

    // ACK handling remains compatible with the current MRP prototype.
    mrp.deriveKey(masterSeed, _lastReceivedSeq + 1, en);

    ack_payload_t ack;
    ack.receiverCounter = _lastReceivedSeq;
    memcpy(ack.newKey, en, AES_KEY_SIZE);
    ack.magic = MAGIC_NUMBER;

    uint8_t ackCipher[64] = { 0 };
    const size_t ackCipherLength = ((sizeof(ack) + 15U) / 16U) * 16U;
    mrp.encrypt((uint8_t*)&ack, sizeof(ack), ackCipher, em);
    esp_now_send(mac, ackCipher, ackCipherLength);
}
