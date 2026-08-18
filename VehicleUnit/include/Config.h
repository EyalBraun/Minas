#ifndef MINAS_VEHICLE_CONFIG_H
#define MINAS_VEHICLE_CONFIG_H

#include <stdint.h>

// personalConfig.h is intentionally untracked and exists only on the local
// development machine. The public repository must build without it.
#if __has_include("personalConfig.h")
#  include "personalConfig.h"
#endif

// ESP32-S3-DevKitC-1 N16R8 mounted on the vehicle.
// Verify the actual wiring before connecting the battery or ESC.
#define SERVO_PIN 25
#define ESC_PIN 26
#define BUZZER_PIN 27
#define SONAR_TRIG_PIN 22
#define SONAR_ECHO_PIN 23

#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE 1000
#define ESC_MAX_PULSE 2000
#define ESC_NEUTRAL_ANGLE 90
#define COMMAND_TIMEOUT_MS 220
#define TELEMETRY_INTERVAL_MS 100
#define SONAR_TIMEOUT_US 12000

// Replace this locally with the Wi-Fi STA MAC of the ESP32-WROVER
// Controller Unit. All-zero values keep the vehicle from sending commands
// to an unconfigured peer.
#ifndef MINAS_CONTROLLER_UNIT_ADDRESS
#define MINAS_CONTROLLER_UNIT_ADDRESS \
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif

static uint8_t controllerUnitAddress[] = MINAS_CONTROLLER_UNIT_ADDRESS;

#endif
