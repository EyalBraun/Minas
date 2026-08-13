/**
 * ============================================================================
 * Project: MinasDT (Data Transmitter)
 * File: Config.h
 * Description: Configuration constants for the Minas RC Car.
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// --- Hardware Pins ---
#define SERVO_PIN 25
#define ESC_PIN 26
#define BUZZER_PIN 27
#define SONAR_TRIG_PIN 22
#define SONAR_ECHO_PIN 23
#define BATTERY_SENSE_PIN 34 // ADC pin to monitor battery voltage

// --- Controller Configuration ---
#define PS5_CONTROLLER_MAC "0c:27:56:21:71:6b"

// --- Telemetry & Timing ---
#define TELEMETRY_INTERVAL_MS 100      // 10Hz telemetry broadcast
#define SONAR_INTERVAL_MS 60           // HC-SR04 measurement period
#define SONAR_TIMEOUT_US 12000         // ~2m range
#define BEEP_INTERVAL_MS 3000          // Mode status beep interval
#define OBSTACLE_BEEP_INTERVAL_MS 250  

// --- Battery Safety (2S LiPo) ---
#define BATTERY_VOLTAGE_MIN 6.8f       // Critical threshold to stop car
#define BATTERY_VOLTAGE_MAX 8.4f       
#define VOLTAGE_DIVIDER_RATIO 2.0f     // Assuming a 10k/10k divider

// --- Servo/ESC PWM Ranges ---
#define SERVO_MIN_PULSE 500
#define SERVO_MAX_PULSE 2400
#define ESC_MIN_PULSE 1000
#define ESC_MAX_PULSE 2000
#define ESC_NEUTRAL_ANGLE 90

// --- ESP-NOW Configuration ---
// REPLACE with your MinasDR Base Station MAC Address
static uint8_t receiverAddress[] = {0xE0, 0x8C, 0xFE, 0x76, 0xD2, 0xD4}; 

#endif
