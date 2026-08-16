/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: DataReceiver.h
 * Description: Receives telemetry and stores one CSV file per labeled trial.
 *
 * A trial is a continuous recording with one fixed label:
 *   1 = owner
 *   0 = non-owner
 *
 * The receiver rotates to a new file when the label in a valid telemetry packet
 * changes. Missing packets are not fabricated as training rows.
 * ============================================================================
 */

#ifndef DATA_RECEIVER_H
#define DATA_RECEIVER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "FS.h"
#include "SD_MMC.h"
#include "Config.h"
#include "../../shared/MRP.h"

class DataReceiver {
public:
    DataReceiver();
    bool begin();
    void handleIncomingData(const uint8_t* mac, const uint8_t* data, int len);

private:
    bool _initialized;
    bool _hasPreviousData;
    unsigned long _lastReceivedSeq;
    telemetry_payload_t _lastData;
    unsigned long _totalLostPackets;

    // Current trial state.
    int _activeLabel;              // -1 = no file yet, 0 = non-owner, 1 = owner
    unsigned long _trialNumber;
    String _activeFilePath;
    File _activeFile;

    bool openNewTrialFile(uint8_t label, const telemetry_payload_t& firstPacket);
    void closeCurrentTrialFile();
    bool writeCsvHeader(uint8_t label, const telemetry_payload_t& firstPacket);
    void logToCurrentTrial(const telemetry_payload_t& data);
    void logPacket(const telemetry_payload_t& data, bool packetLostBeforeThisPacket);
    String makeNextFilePath(uint8_t label);
};

#endif // DATA_RECEIVER_H
