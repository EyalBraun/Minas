/**
 * ============================================================================
 * Project: MinasDR (Data Receiver) - MRP Secure Version
 * File: DataReceiver.h
 * Description: Header for telemetry reception using SD_MMC and MRP.
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
    void handleIncomingData(const uint8_t *mac, const uint8_t *data, int len);

private:
    bool _initialized;
    unsigned long _lastReceivedSeq;
    telemetry_payload_t _lastData; // Using the struct from MRP.h
    int _totalLostPackets;

    void logToCSV(telemetry_payload_t data);
    void checkAndCreateHeader();
};

#endif // DATA_RECEIVER_H
