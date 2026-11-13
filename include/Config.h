#pragma once

#include <stdint.h>

// ============================================================================
// MINAS WROVER DRIVER-RECOGNITION DATA COLLECTOR - HARDWARE & SYSTEM CONFIG
// ============================================================================
// SAFETY PRECAUTION BEFORE LIVE VEHICLE OPERATION:
// Lift the vehicle chassis off the ground with wheels elevated.
// Verify steering servo centering and ESC direction/calibration before enabling
// live motor drive. Perform initial tests at low speed in an enclosed, safe area.

// ----------------------------------------------------------------------------
// 1. BLUETOOTH & CONTROLLER CONFIGURATION
// ----------------------------------------------------------------------------
// Bluetooth MAC address of the paired Sony PS5 DualSense controller.

#define PS5_CONTROLLER_MAC "14:3a:9a:d1:f4:0a"

// ----------------------------------------------------------------------------
// 2. ESP32-WROVER GPIO PIN ASSIGNMENTS
// ----------------------------------------------------------------------------
// HARDWARE PIN CONSTRAINTS FOR ESP32-WROVER:
// - GPIO 16 & 17: Dedicated to internal SPI PSRAM (Pseudo-Static RAM). NEVER use!
// - GPIO 6 to 11: Dedicated to internal SPI Flash memory bus. NEVER use!
// - GPIO 2, 14, 15: Used by the onboard MicroSD card slot in 1-bit SDMMC mode.
// - GPIO 34 to 39: Input-only pins without internal pull-up/pull-down resistors.
//
// Selected Actuator and Feedback Pins:
#define STEERING_SERVO_PIN 25  // Output: 50 Hz PWM control signal for steering servo
#define ESC_PIN            26  // Output: 50 Hz PWM control signal for traction ESC
#define BUZZER_PIN         32  // Output: Piezo buzzer tone signal for acoustic feedback

// ----------------------------------------------------------------------------
// 3. CONTROL LOOP & EXPERIMENT TIMING
// ----------------------------------------------------------------------------
#define SAMPLE_INTERVAL_MS 50UL       // Sampling period: 50 ms = 20 Hz fixed target rate
#define TRIAL_DURATION_MS  600000UL   // Segment duration: 10 minutes (600,000 milliseconds)

// ----------------------------------------------------------------------------
// 4. MICROSD DATA STORAGE (1-BIT SDMMC MODE)
// ----------------------------------------------------------------------------
#define SD_MOUNT_POINT           "/sdcard"  // Virtual File System mount point
#define SD_LOG_DIRECTORY         "/trials"  // Root directory on SD card for CSV logs
#define SD_FLUSH_EVERY_N_SAMPLES 10U        // Flush file buffer to card every 10 samples (500 ms)

// ----------------------------------------------------------------------------
// 5. ACTUATOR & PWM SAFETY LIMITS
// ----------------------------------------------------------------------------
// Steering Servo Range (in degrees and pulse width):
#define STEERING_MIN_DEG    0     // Maximum left turn angle
#define STEERING_MAX_DEG    180   // Maximum right turn angle
#define STEERING_CENTER_DEG 90    // Straight-ahead neutral position

// Steering Servo Pulse Width Range (in microseconds):
// Standard RC servo mapping: 1000 µs = Full Left, 1500 µs = Center (90°), 2000 µs = Full Right.
// Constrained to 1000-2000 µs (instead of 500-2500 µs) to prevent the servo from forcing past
// the chassis physical steering rack limits and stalling the motor.
#define SERVO_MIN_US        1000  // Safe left steering limit (prevents mechanical stall against chassis)
#define SERVO_MAX_US        2000  // Safe right steering limit (prevents mechanical stall against chassis)

// Electronic Speed Controller (ESC) Pulse Width Range (in microseconds):
// Standard RC ESC mapping: 1000 µs = Full Reverse / Brake, 1500 µs = Neutral, 2000 µs = Full Forward
#define ESC_MIN_US          1000  // Full reverse / braking pulse width
#define ESC_NEUTRAL_US      1500  // Safe neutral (motor stopped) pulse width
#define ESC_MAX_US          2000  // Full forward throttle pulse width
#define ESC_FAILSAFE_US     ESC_NEUTRAL_US

// ----------------------------------------------------------------------------
// 6. MOTOR SAFETY LOCK & VERSIONING
// ----------------------------------------------------------------------------
// true  = Live driving mode: Motor receives live throttle commands from controller.
// false = Bench test mode: Motor is locked at neutral (1500 µs), but driver inputs
//         are still computed and recorded to the CSV file for safe offline verification.
#define ENABLE_MOTOR_OUTPUT true

#define FIRMWARE_VERSION "minas-telemetry-v4"

// ----------------------------------------------------------------------------
// 7. EXPERIMENTAL RECORDING PROTOCOL
// ----------------------------------------------------------------------------
// - Circle Button: Start a new 10-minute 'Authorized' (Owner) segment.
// - Square Button: Start a new 10-minute 'Unauthorized' (Non-owner) segment.
// - Cross Button:  If pressed before 10 minutes, stops and preserves the trial with '_under10min' tag.
//                  If pressed after 10 minutes, finalizes and saves the complete trial.
// - Automatic Stop: Segments automatically finalize and save upon reaching 10 minutes.
// - Telemetry data collection only occurs during an active, started segment.

// ----------------------------------------------------------------------------
// 8. OPTIONAL WI-FI NTP CLOCK SYNCHRONIZATION
// ----------------------------------------------------------------------------
// If configured with your Wi-Fi SSID or mobile phone hotspot, the ESP32 will connect
// for 2-3 seconds at boot, sync the real-world date and time from pool.ntp.org, and
// then shut down Wi-Fi immediately before Bluetooth starts (so there is zero RF conflict).
// If left empty, the system automatically uses system uptime (boot_HHhMMmSSs) for filenames.
#define WIFI_NTP_ENABLED        true
#define WIFI_SSID               ""             // Enter your Wi-Fi / phone hotspot name (or leave empty)
#define WIFI_PASSWORD           ""             // Enter your Wi-Fi password (or leave empty)
#define NTP_SERVER              "pool.ntp.org"
#define NTP_TIMEZONE_OFFSET_SEC 7200           // Israel Standard Time UTC+2 (7200 s)
#define NTP_DAYLIGHT_OFFSET_SEC 3600           // Daylight Savings offset (3600 s) or 0

// ----------------------------------------------------------------------------
// 9. DRIVER & TRACK IDENTIFICATION
// ----------------------------------------------------------------------------
// Names recorded in the 'driver_name' CSV column:
#define DRIVER_NAME_AUTHORIZED      "Authorized"   // Circle button (is_authorized = 1)
#define DRIVER_NAME_UNAUTHORIZED    "Unauthorized" // Square button (is_authorized = 0)

// Default track recorded in the 'track' CSV column ('A', 'B', or 'D')
// Can also be changed live on PS5 controller (Up=A, Right=B, Down/Left=D, or Triangle to cycle)
#define DEFAULT_TRACK               'A'



