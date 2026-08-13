/**
 * ============================================================================
 * Project: MinasDT (Data Transmitter)
 * File: main.cpp
 * Description: Main control logic for Minas RC Car with ESP-NOW telemetry.
 *              Simplified version without battery voltage sensing.
 * ============================================================================
 */

#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>
#include <esp_now.h>
#include <WiFi.h>
#include "Config.h"
#include "Buzzer.h"

 // --- Global Objects ---
Servo steeringServo;
Servo throttleESC;

// --- Telemetry Data Structure ---
typedef struct struct_message {
    unsigned long sequenceNumber;
    unsigned long timestamp;
    int throttle;
    int steering;
    int sonarDistance;
    int packetLost;
} struct_message;

struct_message telemetryData;
unsigned long packetCounter = 0;
unsigned long lastTelemetryTime = 0;

// --- State Variables ---
unsigned long lastStatusBeepTime = 0;
unsigned long lastSonarCheckTime = 0;
bool wasConnected = false;
bool isOwnerMode = true;
int frontDistanceCm = 999;

// --- Functions ---

int getSonarDistanceCM() {
    digitalWrite(SONAR_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SONAR_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SONAR_TRIG_PIN, LOW);

    const unsigned long duration = pulseIn(SONAR_ECHO_PIN, HIGH, SONAR_TIMEOUT_US);
    return (duration == 0) ? 999 : static_cast<int>((duration * 0.0343f) / 2.0f);
}

void sendTelemetry(int throttle, int steering) {
    if (millis() - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
        packetCounter++;
        telemetryData.sequenceNumber = packetCounter;
        telemetryData.timestamp = millis();
        telemetryData.throttle = throttle;
        telemetryData.steering = steering;
        telemetryData.sonarDistance = frontDistanceCm;
        telemetryData.packetLost = 0;

        esp_now_send(receiverAddress, (uint8_t*)&telemetryData, sizeof(telemetryData));
        lastTelemetryTime = millis();
    }
}

void handleInput() {
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0);

    // Sonar obstacle check
    if (millis() - lastSonarCheckTime >= SONAR_INTERVAL_MS) {
        frontDistanceCm = getSonarDistanceCM();
        lastSonarCheckTime = millis();
    }

    if (throttleValue > ESC_NEUTRAL_ANGLE && frontDistanceCm <= 30) {
        throttleValue = ESC_NEUTRAL_ANGLE;
        playObstacleSound();
    }

    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);

    // Toggle Owner/Guest mode with Circle
    static bool circlePressed = false;
    if (ps5.Circle()) {
        if (!circlePressed) {
            isOwnerMode = !isOwnerMode;
            Serial.printf("Mode: %s\n", isOwnerMode ? "OWNER" : "GUEST");
            circlePressed = true;
        }
    }
    else {
        circlePressed = false;
    }

    // Broadcast data to receiver
    sendTelemetry(throttleValue, steerAngle);
}

void setup() {
    Serial.begin(115200);

    pinMode(SONAR_TRIG_PIN, OUTPUT);
    pinMode(SONAR_ECHO_PIN, INPUT);

    // ESP-NOW Setup
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW Init Failed");
    }

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);

    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("PS5 Init Failed");
    }

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);

    initBuzzer();
    throttleESC.write(ESC_NEUTRAL_ANGLE);
    delay(2000);
}

void loop() {
    const bool isConnectedNow = ps5.isConnected();

    if (isConnectedNow && !wasConnected) {
        playConnectSound();
        wasConnected = true;
    }
    else if (!isConnectedNow && wasConnected) {
        playDisconnectSound();
        wasConnected = false;
    }

    if (isConnectedNow) {
        handleInput();

        if (millis() - lastStatusBeepTime >= BEEP_INTERVAL_MS) {
            if (isOwnerMode) playOwnerModeBeep();
            else playGuestModeBeep();
            lastStatusBeepTime = millis();
        }
    }
    else {
        throttleESC.write(ESC_NEUTRAL_ANGLE);
    }
}
