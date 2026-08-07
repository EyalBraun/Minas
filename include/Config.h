#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins (Relocated to avoid built-in SDMMC pins 2, 4, 12, 13, 14, 15) ---
#define SERVO_PIN      25
#define ESC_PIN        26
#define BUZZER_PIN     27

// --- Controller Configuration ---
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b" 

// --- Rewind System Configuration ---
#define MAX_HISTORY_STEPS 150     
#define RECORD_INTERVAL_MS 20     

// --- Shadow Stack (Return to Home) Configuration ---
#define MAX_SHADOW_STEPS 5000     

// --- Data Logging Configuration ---
#define LOG_INTERVAL_MS 500       

// --- Servo/ESC PWM Ranges ---
#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE   1000
#define ESC_MAX_PULSE   2000

#endif