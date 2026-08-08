#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>
#include "Config.h"
#include "RewindManager.h"
#include "Buzzer.h"
#include "DataLogger.h"

Servo steeringServo;
Servo throttleESC;
RewindManager chronos(steeringServo, throttleESC);
DataLogger logger;

unsigned long lastRecordTime = 0;
unsigned long lastStatusBeepTime = 0;
bool wasConnected = false;

void setup() {
    Serial.begin(115200);
    chronos.begin();
    logger.begin();

    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("PS5 Init Failed");
    }

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);
    initBuzzer();

    throttleESC.write(90);
    delay(2000);
}

void handleInput() {
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0);

    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);

    // 1. Toggle Owner/Guest mode with Circle button (User's "B")
    static bool circlePressed = false;
    if (ps5.Circle()) {
        if (!circlePressed) {
            bool currentMode = logger.isOwnerMode();
            logger.setOwnerMode(!currentMode);
            Serial.printf("Mode Changed: %s\n", !currentMode ? "OWNER" : "GUEST");
            circlePressed = true;
        }
    } else {
        circlePressed = false;
    }

    // 2. Trigger Manual Rewind with Triangle button
    static bool trianglePressed = false;
    if (ps5.Triangle()) {
        if (!trianglePressed) {
            playRewindSound();
            chronos.startStandardRewind();
            trianglePressed = true;
        }
    } else {
        trianglePressed = false;
    }

    // 3. Periodic sampling for ML features
    if (millis() - lastRecordTime >= RECORD_INTERVAL_MS) {
        chronos.record(steerAngle, throttleValue);
        logger.sample(steerAngle, throttleValue);
        lastRecordTime = millis();
    }
}

void loop() {
    bool isConnectedNow = ps5.isConnected();

    if (isConnectedNow && !wasConnected) {
        playConnectSound();
        logger.logConnect();
        wasConnected = true;
    } else if (!isConnectedNow && wasConnected) {
        playDisconnectSound();
        logger.logDisconnect();
        wasConnected = false;
    }

    if (isConnectedNow) {
        handleInput();
        
        // Periodic status beep every few seconds
        if (millis() - lastStatusBeepTime >= BEEP_INTERVAL_MS) {
            if (logger.isOwnerMode()) {
                playOwnerModeBeep();
            } else {
                playGuestModeBeep();
            }
            lastStatusBeepTime = millis();
        }
    } else {
        // Shadow Stack Return-to-Home logic (Fail-safe)
        // Fixed: Use returnToHome() instead of update()
        chronos.returnToHome();
    }

    logger.update(LOG_INTERVAL_MS);
}
