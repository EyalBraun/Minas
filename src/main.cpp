/**
 * @file main.cpp
 * @project Minas (ChronosDrive Pro)
 * @brief Features MAC Address Verification, Shadow Stack (Return to Home) Fail-safe, and Audio Feedback.
 * 
 * This system controls an RC car using a PS5 controller via Bluetooth.
 * It includes a custom "Rewind" feature (like in racing games) and a failsafe
 * that automatically backtracks the car if the controller loses connection.
 */

#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>
#include "Config.h"
#include "RewindManager.h"
#include "Buzzer.h" 

// --- GLOBAL OBJECTS ---
Servo steeringServo;      // Controls the front steering mechanism
Servo throttleESC;        // Controls the Electronic Speed Controller (Motor)
RewindManager chronos(steeringServo, throttleESC); // Custom class handling history and playback

// --- STATE TRACKING VARIABLES ---
unsigned long lastRecordTime = 0; // Tracks the last time a movement was saved to history
bool wasConnected = false;        // Tracks previous connection state
bool isLoggingPaused = false;     // Controls whether logging/recording is active
unsigned long lastMuteBeepTime = 0; // Tracks interval for warning beeps when logging is paused

void setup() {
    Serial.begin(115200);
    delay(1000);

    /**
     * @brief CRITICAL: Initialize Shadow Stack in PSRAM
     * This allocates the memory buffer required for tracking and replaying history.
     */
    chronos.begin();

    /**
     * @brief MAC Address Enforcement
     * The PS5 library uses this MAC to listen for a specific controller.
     */
    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("Failed to initialize PS5 Library with specified MAC.");
    }

    /**
     * @brief Hardware Timer Allocation
     * Explicitly allocating all 4 timers locks them for the Servo/ESC, 
     * preventing the buzzer tone functions from hijacking the steering channel.
     */
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    
    // Configure and attach Steering Servo
    steeringServo.setPeriodHertz(50);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    
    // Configure and attach ESC
    throttleESC.setPeriodHertz(50);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);

    // Initialize Buzzer with dedicated LEDC channel
    initBuzzer();

    // Arm the ESC with a neutral signal
    throttleESC.write(90);
    delay(2000); 
    
    Serial.println("Minas Ready. Waiting for connection...");
}

/**
 * @brief Reads controller inputs, applies them to the car, and records them to history.
 */
void handleInput() {
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0); 

    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);

    // Save the current state to the Rewind history buffer ONLY if logging is not paused
    if (!isLoggingPaused) {
        if (millis() - lastRecordTime >= RECORD_INTERVAL_MS) {
            chronos.record(steerAngle, throttleValue);
            lastRecordTime = millis();
        }
    }
}

void loop() {
    bool isConnectedNow = ps5.isConnected();

    // --- 1. STATE CHANGE DETECTION (Edge Detection) ---
    if (isConnectedNow && !wasConnected) {
        Serial.println("Controller Connected!");
        playConnectSound(); 
        wasConnected = true; 
    } 
    else if (!isConnectedNow && wasConnected) {
        Serial.println("Connection Lost! Initiating Return to Home...");
        playDisconnectSound(); 
        
        // Trigger Shadow Stack Return to Home fail-safe
        chronos.returnToHome(); 
        wasConnected = false; 
    }

    // --- 2. NORMAL OPERATION ---
    if (isConnectedNow) {
        
        // Toggle logging/recording mode with Cross (X) button
        static bool crossWasPressed = false;
        if (ps5.Cross()) {
            if (!crossWasPressed) {
                isLoggingPaused = !isLoggingPaused;
                crossWasPressed = true;
                Serial.println(isLoggingPaused ? "[Minas] Logging PAUSED." : "[Minas] Logging RESUMED.");
                playMuteWarningSound();
            }
        } else {
            crossWasPressed = false;
        }

        // Check for manual Rewind trigger (Triangle button)
        if (ps5.Triangle()) {
            playRewindSound();            
            chronos.startStandardRewind(); // Execute reverse playback
        } else {
            handleInput();                
        }

        // If logging is paused, beep every 3 seconds to remind the driver
        if (isLoggingPaused) {
            if (millis() - lastMuteBeepTime >= 3000) {
                playMuteWarningSound();
                lastMuteBeepTime = millis();
            }
        }
        
        ps5.setLed(0, 255, 0); 
        ps5.sendToController();
        
    } else {
        // --- 3. SAFETY IDLE ---
        steeringServo.write(90);
        throttleESC.write(90);
    }
    
    delay(5);
}