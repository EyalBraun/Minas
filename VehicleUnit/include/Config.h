#ifndef MINAS_VEHICLE_CONFIG_H
#define MINAS_VEHICLE_CONFIG_H

// DR: ESP32-S3-DevKitC-1 N16R8 mounted on the car.
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

// Replace with the Wi-Fi STA MAC of the WROVER Controller Unit.
static uint8_t controllerUnitAddress[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#endif
