#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>
#include <esp_now.h>
#include <WiFi.h>
#include "Config.h"
#include "Buzzer.h"
#include "../../shared/MRP.h"

Servo steeringServo;
Servo throttleESC;
MRPProtocol mrp;

static unsigned long txCounter = 0;
static unsigned long lastValidCounter = 0;
static unsigned long lastTelemetryTime = 0;
static unsigned long lastAckTime = 0;
static unsigned long lastPacketTime = 0;
static unsigned long lastSonarCheckTime = 0;
static unsigned long lastStatusBeepTime = 0;
static int failureCounter = 0;
static int lastThrottle = ESC_NEUTRAL_ANGLE;
static int lastSteering = 90;
static int frontDistanceCm = 999;
static bool wasConnected = false;
static bool isOwnerMode = true;
static bool radioReady = false;
static bool controllerReady = false;
static bool hasReceivedAck = false;

static uint8_t masterSeed[MASTER_SEED_SIZE] = {
    0x4D, 0x49, 0x4E, 0x41, 0x5F, 0x53, 0x45, 0x43,
    0x52, 0x45, 0x54, 0x5F, 0x53, 0x45, 0x45, 0x44,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

static uint8_t currentKey[AES_KEY_SIZE];

static bool receiverMacConfigured() {
    for (int i = 0; i < 6; ++i) {
        if (receiverAddress[i] != 0x00) return true;
    }
    return false;
}

static void safeNeutral() {
    throttleESC.write(ESC_NEUTRAL_ANGLE);
}

static int getSonarDistanceCM() {
    digitalWrite(SONAR_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SONAR_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SONAR_TRIG_PIN, LOW);

    const unsigned long duration =
        pulseIn(SONAR_ECHO_PIN, HIGH, SONAR_TIMEOUT_US);
    return (duration == 0)
        ? 999
        : static_cast<int>((duration * 0.0343f) / 2.0f);
}

static void sendTelemetry(int throttle, int steering) {
    const unsigned long now = millis();
    if (now - lastTelemetryTime < TELEMETRY_INTERVAL_MS) return;

    const unsigned long dt =
        (lastPacketTime == 0) ? 0 : (now - lastPacketTime);

    txCounter++;

    telemetry_payload_t payload{};
    payload.sequenceNumber = txCounter;
    payload.timestamp = now;
    payload.throttle = throttle;
    payload.steering = steering;
    payload.sonarDistance = frontDistanceCm;
    payload.packetLost = 0;
    payload.isOwner = isOwnerMode ? 1 : 0;
    payload.deltaTime = dt;
    payload.steerVelocity = (dt > 0)
        ? static_cast<float>(steering - lastSteering) / dt : 0.0f;
    payload.throttleVelocity = (dt > 0)
        ? static_cast<float>(throttle - lastThrottle) / dt : 0.0f;
    payload.magic = MAGIC_NUMBER;

    uint8_t cipherBuffer[64] = {0};
    const size_t cipherLength = ((sizeof(payload) + 15U) / 16U) * 16U;
    mrp.encrypt(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload),
                cipherBuffer, currentKey);

    const esp_err_t result =
        esp_now_send(receiverAddress, cipherBuffer, cipherLength);
    if (result != ESP_OK) {
        Serial.printf("[WARN] esp_now_send telemetry failed: %d\n", result);
    }

    lastTelemetryTime = now;
    lastPacketTime = now;
    lastThrottle = throttle;
    lastSteering = steering;
}

static void OnDataRecv(const uint8_t* mac, const uint8_t* incomingData, int len) {
    (void)mac;

    const size_t expectedAckLength =
        ((sizeof(ack_payload_t) + 15U) / 16U) * 16U;
    if (incomingData == nullptr || len != static_cast<int>(expectedAckLength)) {
        failureCounter++;
        return;
    }

    uint8_t decryptedBuffer[64] = {0};
    if (!mrp.decrypt(incomingData, len, decryptedBuffer, currentKey)) {
        failureCounter++;
        return;
    }

    ack_payload_t ack{};
    memcpy(&ack, decryptedBuffer, sizeof(ack));
    if (ack.magic != MAGIC_NUMBER || ack.receiverCounter != txCounter) {
        failureCounter++;
        return;
    }

    memcpy(currentKey, ack.newKey, AES_KEY_SIZE);
    lastValidCounter = txCounter;
    lastAckTime = millis();
    hasReceivedAck = true;
    failureCounter = 0;
}

static void handleInput() {
    int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0);

    const unsigned long now = millis();
    if (now - lastSonarCheckTime >= SONAR_INTERVAL_MS) {
        frontDistanceCm = getSonarDistanceCM();
        lastSonarCheckTime = now;
    }

    if (throttleValue > ESC_NEUTRAL_ANGLE && frontDistanceCm <= 30) {
        throttleValue = ESC_NEUTRAL_ANGLE;
        playObstacleSound();
    }

    static bool circlePressed = false;
    if (ps5.Circle()) {
        if (!circlePressed) {
            // Mode changes are accepted only while the car is neutral.
            safeNeutral();
            isOwnerMode = !isOwnerMode;
            circlePressed = true;
            if (isOwnerMode) playOwnerModeBeep();
            else playGuestModeBeep();
        }
    } else {
        circlePressed = false;
    }

    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);
    sendTelemetry(throttleValue, steerAngle);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    safeNeutral();

    if (!receiverMacConfigured()) {
        Serial.println("[FATAL] Replace receiverAddress in Config.h");
        return;
    }

    mrp.deriveKey(masterSeed, 0, currentKey);

    pinMode(SONAR_TRIG_PIN, OUTPUT);
    pinMode(SONAR_ECHO_PIN, INPUT);

    WiFi.mode(WIFI_STA);
    const esp_err_t radioResult = esp_now_init();
    if (radioResult != ESP_OK) {
        Serial.printf("[FATAL] esp_now_init failed: %d\n", radioResult);
        return;
    }

    esp_now_peer_info_t peerInfo{};
    memcpy(peerInfo.peer_addr, receiverAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    const esp_err_t peerResult = esp_now_add_peer(&peerInfo);
    if (peerResult != ESP_OK && peerResult != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("[FATAL] esp_now_add_peer failed: %d\n", peerResult);
        return;
    }

    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    radioReady = true;

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);
    initBuzzer();

    ps5.begin(PS5_CONTROLLER_MAC);
    controllerReady = true;
    Serial.println("[READY] MinasDT initialized");
}

void loop() {
    if (!radioReady || !controllerReady) {
        safeNeutral();
        delay(10);
        return;
    }

    if (ps5.isConnected()) {
        if (!wasConnected) {
            playConnectSound();
            wasConnected = true;
            lastAckTime = millis();
            hasReceivedAck = false;
        }

        if (hasReceivedAck && millis() - lastAckTime > ACK_TIMEOUT_MS) {
            // Fail-safe: no recent receiver acknowledgement means no drive.
            safeNeutral();
        } else {
            handleInput();
        }

        if (millis() - lastStatusBeepTime >= BEEP_INTERVAL_MS) {
            if (isOwnerMode) playOwnerModeBeep();
            else playGuestModeBeep();
            lastStatusBeepTime = millis();
        }
    } else {
        if (wasConnected) {
            playDisconnectSound();
            wasConnected = false;
        }
        safeNeutral();
    }
}
