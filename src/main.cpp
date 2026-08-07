/**
 * @file main.cpp
 * @project Minas (ChronosDrive Pro)
 * @brief FreeRTOS-enabled multi-core architecture for real-time control, background logging, and fail-safe execution.
 */

#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>

// --- FreeRTOS Headers ---
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "Config.h"
#include "RewindManager.h"
#include "Buzzer.h"
#include "DataLogger.h"

// --- GLOBAL OBJECTS ---
Servo steeringServo;      
Servo throttleESC;        
RewindManager chronos(steeringServo, throttleESC); 
DataLogger dataLogger;

// --- STATE TRACKING & THREAD SAFETY ---
int currentSteerAngle = 90; 
int currentThrottleValue = 90;
unsigned long lastRecordTime = 0; 
bool wasConnected = false;

SemaphoreHandle_t stateMutex;

/**
 * @brief FreeRTOS Background Logging Task (Pinned to Core 1)
 * Handles heavy SD card writes asynchronously without blocking Core 0 or Bluetooth.
 */
void backgroundLoggingTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // Sample every 50ms

    while (true) {
        int s, t;
        if (xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE) {
            s = currentSteerAngle;
            t = currentThrottleValue;
            xSemaphoreGive(stateMutex);
        }

        dataLogger.sample(s, t);
        dataLogger.update(LOG_INTERVAL_MS);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Create mutex for thread-safe core communication
    stateMutex = xSemaphoreCreateMutex();

    // 1. Initialize SD Card Logger
    dataLogger.begin();

    // 2. Initialize Shadow Stack in PSRAM
    chronos.begin();

    // 3. Initialize PS5 Controller
    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("Failed to initialize PS5 Library with specified MAC.");
    }

    // Allocate hardware timers for Servo & ESC PWM
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);
    
    steeringServo.setPeriodHertz(50);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    
    throttleESC.setPeriodHertz(50);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);

    throttleESC.write(90);
    delay(2000); 

    // 4. Initialize Buzzer AFTER servos so LEDC channels don't get overwritten
    initBuzzer();

    // 5. Create background logging task on Core 1 (Keeping Core 0 free for Bluetooth)
    xTaskCreatePinnedToCore(
        backgroundLoggingTask,   
        "LogTask",               
        4096,                    
        NULL,                    
        1,                       
        NULL,                    
        1                        
    );

    Serial.println("Minas System Online (FreeRTOS Multi-Core Active).");
}

void handleInput() {
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0); 

    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);

    // Safely update shared state for the FreeRTOS logging task
    if (xSemaphoreTake(stateMutex, 0) == pdTRUE) {
        currentSteerAngle = steerAngle;
        currentThrottleValue = throttleValue;
        xSemaphoreGive(stateMutex);
    }

    // Record history for the Rewind / Shadow Stack system
    if (millis() - lastRecordTime >= RECORD_INTERVAL_MS) {
        chronos.record(steerAngle, throttleValue);
        lastRecordTime = millis();
    }
}

void loop() {
    bool isConnectedNow = ps5.isConnected();

    // --- 1. STATE CHANGE DETECTION (Edge Detection) ---
    if (isConnectedNow && !wasConnected) {
        Serial.println("Controller Connected!");
        playConnectSound(); 
        dataLogger.logConnect();
        wasConnected = true; 
    } 
    else if (!isConnectedNow && wasConnected) {
        Serial.println("Connection Lost! Initiating Return to Home...");
        playDisconnectSound(); 
        dataLogger.logDisconnect();
        
        // Trigger Shadow Stack Return to Home fail-safe
        chronos.returnToHome(); 
        
        wasConnected = false; 
    }

    // --- 2. NORMAL OPERATION ---
    if (isConnectedNow) {
        if (ps5.Triangle()) {
            playRewindSound();            
            dataLogger.logRewind();
            chronos.startStandardRewind(); 
        } else {
            handleInput();               
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