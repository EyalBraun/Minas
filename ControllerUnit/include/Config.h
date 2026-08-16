#ifndef MINAS_CONTROLLER_CONFIG_H
#define MINAS_CONTROLLER_CONFIG_H

#include <stdint.h>

// DT: original ESP32/WROVER with PS5 Bluetooth Classic.
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b"

// Replace with the Wi-Fi STA MAC of the ESP32-S3 Vehicle Unit.
static uint8_t vehicleUnitAddress[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// External microSD module connected to the WROVER using SPI.
// Change these pins to match the actual SD wiring.
#define SD_SCK_PIN 18
#define SD_MISO_PIN 19
#define SD_MOSI_PIN 23
#define SD_CS_PIN 5
#define BUZZER_PIN 27

#define TELEMETRY_INTERVAL_MS 50
#define VEHICLE_TELEMETRY_TIMEOUT_MS 500
#define COMMAND_TTL_MS 180

// 1 = collection bypass, 2 = shadow ML, 3 = enforced ML.
#define CONTROLLER_OPERATION_MODE 1
#define INITIAL_OWNER_LABEL 1

#define SD_LOG_PREFIX "/trial_"
#define ESC_NEUTRAL_ANGLE 90

#endif
