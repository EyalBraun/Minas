#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
#define SERVO_PIN 25
#define ESC_PIN 26
#define BUZZER_PIN 27
#define SONAR_TRIG_PIN 22
#define SONAR_ECHO_PIN 23

// --- Controller Configuration ---
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b"

// --- Logging and Safety ---
#define RECORD_INTERVAL_MS 20          // Controller sampling rate (50 Hz)
#define LOG_INTERVAL_MS 500            // CSV log writing interval
#define FRICTION_COEFFICIENT 0.85      // Estimated friction factor
#define BEEP_INTERVAL_MS 3000          // Owner/guest status beep interval

#define OBSTACLE_DISTANCE_CM 30        // Stop when a front obstacle is at or below this distance
#define SONAR_INTERVAL_MS 60           // HC-SR04 measurement period; do not measure continuously
#define SONAR_TIMEOUT_US 12000         // 12 ms timeout, approximately 2 m maximum measured distance
#define OBSTACLE_BEEP_INTERVAL_MS 250  // Repeat obstacle warning no more often than every 250 ms

// --- Servo/ESC PWM Ranges ---
#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE 1000
#define ESC_MAX_PULSE 2000

#define ESC_NEUTRAL_ANGLE 90

#endif
