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
unsigned long lastRecordTime = 0; // Tracks the last time a movement was saved to history (for non-blocking delays)
bool wasConnected = false;        // Tracks previous connection state to detect exact moment of connect/disconnect

void setup() {
    Serial.begin(115200);

    /**
     * @brief MAC Address Enforcement
     * The PS5 library uses this MAC to listen for a specific controller.
     * Ensure this matches your controller's actual Bluetooth MAC.
     */
    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("Failed to initialize PS5 Library with specified MAC.");
    }

    /**
     * @brief CRITICAL: Hardware Timer Allocation
     * The ESP32 uses internal hardware timers for PWM signals. 
     * The tone() function (used by the buzzer) and the ESP32Servo library share these timers.
     * By explicitly allocating all 4 timers here, we lock them for the Servo/ESC, 
     * preventing tone() from hijacking the steering channel and causing the servo to freeze.
     */
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    
    // Configure and attach Steering Servo (50Hz is the standard refresh rate for RC servos)
    steeringServo.setPeriodHertz(50);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    
    // Configure and attach ESC (Electronic Speed Controller)
    throttleESC.setPeriodHertz(50);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);

    // Send a neutral signal (90 degrees) to the ESC to "arm" it.
    // Most ESCs require a neutral signal for a few seconds on startup for safety.
    throttleESC.write(90);
    delay(2000); 
    
    Serial.println("Minas Ready. Waiting for connection...");
}

/**
 * @brief Reads controller inputs, applies them to the car, and records them to history.
 */
void handleInput() {
    // Read joystick values (range: -128 to 127) and map them to standard servo degrees (0 to 180)
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0); // Note: Throttle mapped in reverse based on stick orientation

    // Send the mapped values to the physical hardware
    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);

    // Save the current state to the Rewind history buffer.
    // We use millis() instead of delay() to avoid blocking the code.
    if (millis() - lastRecordTime >= RECORD_INTERVAL_MS) {
        chronos.record(steerAngle, throttleValue);
        lastRecordTime = millis();
    }
}

void loop() {
    // Check current connection status of the PS5 controller
    bool isConnectedNow = ps5.isConnected();

    // --- 1. STATE CHANGE DETECTION (Edge Detection) ---
    // We only want to trigger these actions exactly when the state changes, not continuously.
    
    if (isConnectedNow && !wasConnected) {
        // Event: Controller just connected
        Serial.println("Controller Connected!");
        playConnectSound(); // נקרא מתוך Buzzer.h
        wasConnected = true; // Update state flag
    } 
    else if (!isConnectedNow && wasConnected) {
        // Event: Controller just disconnected (Connection Lost / Out of Range)
        Serial.println("Connection Lost! Initiating Return to Home...");
        playDisconnectSound(); // נקרא מתוך Buzzer.h
        
        /**
         * @brief Shadow Stack Fail-safe (Return to Home)
         * If signal drops, the car automatically plays its history in reverse 
         * to drive back into Bluetooth range.
         */
        chronos.returnToHome(); 
        wasConnected = false; // Update state flag so we don't repeat this loop
    }

    // --- 2. NORMAL OPERATION ---
    if (isConnectedNow) {
        
        // Check for manual Rewind trigger (Triangle button)
        if (ps5.Triangle()) {
            playRewindSound();             // נקרא מתוך Buzzer.h
            chronos.startStandardRewind(); // Execute reverse playback
        } else {
            handleInput();                 // Drive normally if no buttons are pressed
        }
        
        // Visual feedback on the PS5 controller itself (Set lightbar to Green)
        ps5.setLed(0, 255, 0); 
        ps5.sendToController();
        
    } else {
        // --- 3. SAFETY IDLE ---
        // If disconnected (and after Return to Home finishes), ensure the car stays completely still.
        // Sending 90 degrees puts both the steering and the motor into neutral.
        steeringServo.write(90);
        throttleESC.write(90);
    }
    
    // Small delay to maintain stability and prevent overwhelming the Bluetooth bus
    delay(5);
}