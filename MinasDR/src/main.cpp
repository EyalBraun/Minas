/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: MinasDR.ino
 * Description: Main entry point for the SD_MMC base station.
 * ============================================================================
 */

#include "../include/DataReceiver.h"
#include <Arduino.h>
DataReceiver receiver;

void OnDataRecv(const uint8_t* mac, const uint8_t* incomingDataBytes, int len) {
    receiver.handleIncomingData(mac, incomingDataBytes, len);
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n--- Starting MinasDR (SD_MMC) ---");

    if (receiver.begin()) {
        esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
        Serial.println("[READY] Waiting for telemetry...");
    }
    else {
        Serial.println("[FATAL] Init failed.");
    }
}

void loop() {
    // Event-driven
}
