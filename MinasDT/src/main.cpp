/**
 * ============================================================================
 * Project: MinasDT (Data Transmitter) - MRP v1.0 Hardened with ML Features
 * File: main.cpp
 * Description: RC Car firmware with KDF-based MRP and behavioral telemetry.
 * ============================================================================
 */

#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>
#include <esp_now.h>
#include <WiFi.h>
#include "Config.h"
#include "Buzzer.h"
#include "../../shared/MRP.h"

 // --- Global Objects ---
Servo steeringServo;
Servo throttleESC;
MRPProtocol mrp;

// --- Security State Variables ---
unsigned long tmc = 0;
unsigned long lastValidMc = 0;
int failureCounter = 0;

uint8_t masterSeed[MASTER_SEED_SIZE] = {
    0x4D, 0x49, 0x4E, 0x41, 0x5F, 0x53, 0x45, 0x43,
    0x52, 0x45, 0x54, 0x5F, 0x53, 0x45, 0x45, 0x44,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

uint8_t em[AES_KEY_SIZE];

// --- Behavioral Telemetry State ---
int lastThrottle = 0;
int lastSteering = 0;
unsigned long lastPacketTime = 0;

// --- Operational Variables ---
unsigned long lastTelemetryTime = 0;
unsigned long lastSonarCheckTime = 0;
unsigned long lastStatusBeepTime = 0;
bool wasConnected = false;
bool isOwnerMode = true;
int frontDistanceCm = 999;

// --- Forward Declarations ---
void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len);

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
        unsigned long now = millis();
        unsigned long dt = (lastPacketTime == 0) ? 0 : (now - lastPacketTime);

        // --- Resynchronization Logic ---
        if (failureCounter >= FAILURE_THRESHOLD) {
            mrp.deriveKey(masterSeed, lastValidMc, em);
            failureCounter = 0;
        }

        tmc++;

        telemetry_payload_t payload;
        payload.sequenceNumber = tmc;
        payload.timestamp = now;
        payload.throttle = throttle;
        payload.steering = steering;
        payload.sonarDistance = frontDistanceCm;
        payload.packetLost = 0;
        payload.isOwner = isOwnerMode ? 1 : 0;

        // --- Calculate ML Behavioral Features ---
        payload.deltaTime = dt;
        payload.steerVelocity = (dt > 0) ? (float)abs(steering - lastSteering) / dt : 0;
        payload.throttleVelocity = (dt > 0) ? (float)abs(throttle - lastThrottle) / dt : 0;

        payload.magic = MAGIC_NUMBER;

        uint8_t cipherBuffer[64] = { 0 };
        mrp.encrypt((uint8_t*)&payload, sizeof(payload), cipherBuffer, em);

        esp_now_send(receiverAddress, cipherBuffer, sizeof(payload));

        // Update State
        lastTelemetryTime = now;
        lastPacketTime = now;
        lastThrottle = throttle;
        lastSteering = steering;
    }
}

void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
    uint8_t decryptedBuffer[64] = { 0 };
    if (mrp.decrypt(incomingData, len, decryptedBuffer, em)) {
        ack_payload_t* ack = (ack_payload_t*)decryptedBuffer;
        if (ack->receiverCounter == tmc) {
            memcpy(em, ack->newKey, AES_KEY_SIZE);
            lastValidMc = tmc;
            failureCounter = 0;
        }
    }
    else {
        failureCounter++;
    }
}

void handleInput() {
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0);

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

    static bool circlePressed = false;
    if (ps5.Circle()) {
        if (!circlePressed) {
            isOwnerMode = !isOwnerMode;
            circlePressed = true;
            if (isOwnerMode) playOwnerModeBeep();
            else playGuestModeBeep();
        }
    }
    else {
        circlePressed = false;
    }

    sendTelemetry(throttleValue, steerAngle);
}

void setup() {
    Serial.begin(115200);
    mrp.deriveKey(masterSeed, 0, em);

    pinMode(SONAR_TRIG_PIN, OUTPUT);
    pinMode(SONAR_ECHO_PIN, INPUT);

    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));

    ps5.begin(PS5_CONTROLLER_MAC);
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);

    initBuzzer();
    throttleESC.write(ESC_NEUTRAL_ANGLE);
    delay(2000);
}

void loop() {
    if (ps5.isConnected()) {
        if (!wasConnected) {
            playConnectSound();
            wasConnected = true;
        }
        handleInput();

        if (millis() - lastStatusBeepTime >= 3000) {
            if (isOwnerMode) playOwnerModeBeep();
            else playGuestModeBeep();
            lastStatusBeepTime = millis();
        }
    }
    else {
        if (wasConnected) {
            playDisconnectSound();
            wasConnected = false;
        }
        throttleESC.write(ESC_NEUTRAL_ANGLE);
    }
}
