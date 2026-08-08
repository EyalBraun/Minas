#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
#define SERVO_PIN 25
#define ESC_PIN 26
#define BUZZER_PIN 27

// --- Controller Configuration ---
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b"

// --- ML Data Collection Configuration ---
#define RECORD_INTERVAL_MS 20      // Sampling rate for features (50Hz)
#define LOG_INTERVAL_MS 500        // Logging interval to SD card
#define FRICTION_COEFFICIENT 0.85  // Learned friction factor for home environment
#define BEEP_INTERVAL_MS 3000      // Periodic beep interval for status feedback

// --- Rewind & Shadow Stack ---
#define MAX_HISTORY_STEPS 150
#define MAX_SHADOW_STEPS 5000

// --- Servo/ESC PWM Ranges ---
#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE 1000
#define ESC_MAX_PULSE 2000

#endif
