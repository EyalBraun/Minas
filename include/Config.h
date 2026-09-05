#pragma once

#include <stdint.h>

// ============================================================================
// MINAS WROVER DRIVER-RECOGNITION DATA COLLECTOR - HARDWARE & FIRMWARE CONFIG
// ============================================================================

// ----------------------------------------------------------------------------
// 1. BLUETOOTH & CONTROLLER SETTINGS
// ----------------------------------------------------------------------------
// PS5 DualSense Bluetooth MAC address. Replace with your controller's MAC.
#define PS5_CONTROLLER_MAC "00:00:00:00:00:00"

// Binary experiment initial ground-truth label:
// 1 = owner, 0 = nonowner.
#define INITIAL_OWNER_LABEL 1

// ----------------------------------------------------------------------------
// 2. ESP32-WROVER GPIO ASSIGNMENTS & PIN CONSTRAINTS
// ----------------------------------------------------------------------------
// IMPORTANT PIN CONSTRAINTS FOR ESP32-WROVER:
// - GPIO 16 & 17: Reserved for internal PSRAM (SPI RAM). Never reassign!
// - GPIO 6-11:    Connected to internal SPI Flash. Never reassign!
// - GPIO 2, 14, 15: Used by onboard MicroSD slot in 1-bit SDMMC Mode.
// - GPIO 34-39:   Input-only pins (no internal pull-up/pull-down resistors).
//
// Selected Pinout:
#define STEERING_SERVO_PIN 25  // Output: 50 Hz PWM control signal for steering servo
#define ESC_PIN            26  // Output: 50 Hz PWM control signal for electronic speed controller
#define SONAR_TRIG_PIN     27  // Output: 10 µs trigger pulse for HC-SR04 ultrasonic sensor
#define SONAR_ECHO_PIN     33  // Input:  Echo return pulse (MUST use voltage divider for 3.3V!)
#define BUZZER_PIN         32  // Output: Piezo buzzer tone signal

// ----------------------------------------------------------------------------
// 3. HC-SR04 ULTRASONIC SENSOR TIMING & CONSTRAINTS
// ----------------------------------------------------------------------------
#define SONAR_TIMEOUT_US         25000UL  // 25 ms timeout (~430 cm maximum obstacle distance)
#define SONAR_SAMPLE_INTERVAL_MS 100UL    // Sample sonar every 100 ms (10 Hz) to avoid echo reverberation

// ----------------------------------------------------------------------------
// 4. MICROSD LOGGING CONFIGURATION (SDMMC 1-BIT MODE)
// ----------------------------------------------------------------------------
#define SD_MOUNT_POINT           "/sdcard"
#define SD_LOG_DIRECTORY         "/trials"
#define SD_FLUSH_EVERY_N_SAMPLES 10U      // Flush to SD card every 10 samples (500 ms)

// ----------------------------------------------------------------------------
// 5. CONTROL LOOP TIMING
// ----------------------------------------------------------------------------
// Target sampling rate for control and data logging: 50 ms = 20 Hz.
#define SAMPLE_INTERVAL_MS       50UL

// ----------------------------------------------------------------------------
// 6. ACTUATOR & PWM SAFETY LIMITS
// ----------------------------------------------------------------------------
#define STEERING_MIN_DEG         0
#define STEERING_MAX_DEG         180
#define STEERING_CENTER_DEG      90

#define ESC_MIN_US               1000
#define ESC_NEUTRAL_US           1500
#define ESC_MAX_US               2000
#define ESC_FAILSAFE_US          ESC_NEUTRAL_US

// ----------------------------------------------------------------------------
// 7. HARDWARE SAFETY LOCK
// ----------------------------------------------------------------------------
// false = Bench test mode: Logs intended driver commands to CSV, but keeps
//         physical ESC signal locked at 1500 µs (neutral) for safety.
// true  = Active drive mode: Sends live throttle PWM to the ESC motor.
#define ENABLE_MOTOR_OUTPUT      false
