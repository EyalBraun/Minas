/**
 * ============================================================================
 * Project: MinasDT (Data Transmitter)
 * File: Config.h
 * Description: Configuration constants for the Minas RC Car.
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// Hardware pins.
#define SERVO_PIN 25
#define ESC_PIN 26
#define BUZZER_PIN 27
#define SONAR_TRIG_PIN 22
#define SONAR_ECHO_PIN 23

// Battery monitoring is deliberately disabled until the voltage divider is
// physically installed and verified with a multimeter.
#define BATTERY_ADC_PIN 34
#define BATTERY_MONITOR_ENABLED 0
#define BATTERY_CUTOFF_MV 6800
#define BATTERY_DIVIDER_RATIO 2.0f

// PS5 controller Bluetooth MAC address.
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b"

// Telemetry and safety timing.
#define TELEMETRY_INTERVAL_MS 100
#define SONAR_INTERVAL_MS 60
#define SONAR_TIMEOUT_US 12000
#define BEEP_INTERVAL_MS 3000
#define OBSTACLE_BEEP_INTERVAL_MS 250
#define ACK_TIMEOUT_MS 1000

// Servo and ESC PWM ranges.
#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE 1000
#define ESC_MAX_PULSE 2000
#define ESC_NEUTRAL_ANGLE 90

// Replace every byte with the real MinasDR station MAC address.
// Do not leave this as FF:FF:FF:FF:FF:FF during normal operation.
static uint8_t receiverAddress[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#endif // CONFIG_H
