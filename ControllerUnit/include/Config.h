#ifndef MINAS_CONTROLLER_CONFIG_H
#define MINAS_CONTROLLER_CONFIG_H

#include <stdint.h>

// personalConfig.h is intentionally untracked and exists only on the local
// development machine. The public repository must build without it.
#if __has_include("personalConfig.h")
#  include "personalConfig.h"
#endif

// Original ESP32/WROVER board with Bluetooth Classic for the DualSense.
#ifndef MINAS_PS5_CONTROLLER_MAC
#define MINAS_PS5_CONTROLLER_MAC "00:00:00:00:00:00"
#endif

#define PS5_CONTROLLER_MAC MINAS_PS5_CONTROLLER_MAC

// Replace this locally with the Wi-Fi STA MAC of the ESP32-S3 Vehicle Unit.
// All-zero values intentionally keep the Controller Unit in local PS5 + SD
// test mode and prevent ESP-NOW command transmission.
#ifndef MINAS_VEHICLE_UNIT_ADDRESS
#define MINAS_VEHICLE_UNIT_ADDRESS \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif

static uint8_t vehicleUnitAddress[] = MINAS_VEHICLE_UNIT_ADDRESS;

// External microSD module connected to the WROVER using SPI.
#define SD_SCK_PIN 18
#define SD_MISO_PIN 19
#define SD_MOSI_PIN 23
#define SD_CS_PIN 5
#define BUZZER_PIN 27

#define TELEMETRY_INTERVAL_MS 50
#define VEHICLE_TELEMETRY_TIMEOUT_MS 500
#define COMMAND_TTL_MS 180
#define COMMAND_ACK_TIMEOUT_MS 160
#define ML_MIN_CONFIDENCE_PERMILLE 700

// 1 = collection bypass, 2 = shadow ML, 3 = enforced ML.
#define CONTROLLER_OPERATION_MODE 1
#define INITIAL_OWNER_LABEL 1

// Replace these locally per collection session. They are written into every CSV.
#ifndef MINAS_SESSION_ID
#define MINAS_SESSION_ID "session_unknown"
#endif
#ifndef MINAS_DRIVER_ID
#define MINAS_DRIVER_ID "driver_unknown"
#endif
#ifndef MINAS_ROUTE_ID
#define MINAS_ROUTE_ID "route_unknown"
#endif

#define SD_LOG_PREFIX "/trial_"
#define ESC_NEUTRAL_ANGLE 90

#endif
