/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: DataReceiver.h
 * Description: Header for telemetry reception using SD_MMC.
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

 // Telemetry packet structure
typedef struct struct_message {
    unsigned long sequenceNumber;
    unsigned long timestamp;
    int throttle;
    int steering;
    int sonarDistance;
    int packetLost;
} struct_message;

class DataReceiver {
public:
    DataReceiver();
    bool begin();
    void handleIncomingData(const uint8_t* mac, const uint8_t* data, int len);

private:
    bool _initialized;
    unsigned long _lastReceivedSeq;
    struct_message _lastData;
    int _totalLostPackets;

    void logToCSV(struct_message data);
    void checkAndCreateHeader();
};

#endif // DATA_RECEIVER_H
