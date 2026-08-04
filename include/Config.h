#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
#define SERVO_PIN 4
#define ESC_PIN   5
#define BUZZER_PIN 13

// --- Controller Configuration ---
// IMPORTANT: Use the REAL MAC address of your PS5 controller here.
// You can find it by pairing the controller to your phone and checking device info.
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b" 

// --- Rewind System Configuration ---
#define MAX_HISTORY_STEPS 150    // Standard Rewind (approx 3 seconds)
#define RECORD_INTERVAL_MS 20    

// --- Shadow Stack (Return to Home) Configuration ---
// The Shadow Stack stores the entire journey. 
// Wrover has enough PSRAM for a very large buffer.
#define MAX_SHADOW_STEPS 5000    // Records up to 100 seconds of movement

// --- Servo/ESC PWM Ranges ---
#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE   1000
#define ESC_MAX_PULSE   2000

#endif
