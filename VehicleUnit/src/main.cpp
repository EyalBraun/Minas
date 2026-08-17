#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Config.h"
#include "../../shared/MRP.h"

Servo steeringServo;
Servo throttleESC;
MRPProtocol mrp;
uint8_t rollingKey[AES_KEY_SIZE] = {};
uint32_t lastAcceptedSequence = 0;
uint32_t lastAcceptedCommandMs = 0;
uint32_t lastReceivedPacketMs = 0;
uint32_t lastTelemetryMs = 0;
int appliedSteering = 90;
int appliedThrottle = ESC_NEUTRAL_ANGLE;
int sonarDistanceCm = 999;

bool validControllerMacConfigured() {
    for (uint8_t byte : controllerUnitAddress) if (byte != 0x00) return true;
    return false;
}

void safeNeutral() {
    appliedSteering = 90;
    appliedThrottle = ESC_NEUTRAL_ANGLE;
    steeringServo.write(90);
    throttleESC.write(ESC_NEUTRAL_ANGLE);
}

int readSonarCm() {
    digitalWrite(SONAR_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SONAR_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SONAR_TRIG_PIN, LOW);
    const unsigned long duration = pulseIn(SONAR_ECHO_PIN, HIGH, SONAR_TIMEOUT_US);
    return duration == 0 ? 999 : static_cast<int>((duration * 0.0343f) / 2.0f);
}

bool sendAck(uint32_t acceptedCounter, const uint8_t nextKey[AES_KEY_SIZE],
             const uint8_t currentKey[AES_KEY_SIZE]) {
    ack_payload_t ack{};
    ack.receiverCounter = acceptedCounter;
    memcpy(ack.newKey, nextKey, AES_KEY_SIZE);
    ack.magic = MAGIC_NUMBER;

    uint8_t ciphertext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    size_t ciphertextLength = 0;
    if (!mrp.encrypt(reinterpret_cast<const uint8_t*>(&ack), sizeof(ack),
                     ciphertext, currentKey, ciphertextLength)) {
        Serial.println("[MRP] ACK encryption failed");
        return false;
    }
    return esp_now_send(controllerUnitAddress, ciphertext, ciphertextLength) == ESP_OK;
}

void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (mac == nullptr || data == nullptr ||
        memcmp(mac, controllerUnitAddress, 6) != 0 ||
        len != static_cast<int>(mrpPaddedLength(sizeof(telemetry_payload_t)))) return;

    uint8_t plaintext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    if (!mrp.decrypt(data, static_cast<size_t>(len), plaintext, rollingKey)) return;

    telemetry_payload_t payload{};
    memcpy(&payload, plaintext, sizeof(payload));
    if (payload.magic != MAGIC_NUMBER || payload.sequenceNumber <= lastAcceptedSequence) return;

    const uint8_t currentKey[AES_KEY_SIZE] = {
        rollingKey[0], rollingKey[1], rollingKey[2], rollingKey[3],
        rollingKey[4], rollingKey[5], rollingKey[6], rollingKey[7],
        rollingKey[8], rollingKey[9], rollingKey[10], rollingKey[11],
        rollingKey[12], rollingKey[13], rollingKey[14], rollingKey[15]
    };
    uint8_t nextKey[AES_KEY_SIZE] = {};
    mrp.deriveKey(MRP_MASTER_SEED, payload.sequenceNumber + 1U, nextKey);

    // ACK is encrypted with the key used for the accepted packet. Only after
    // constructing/sending it do both sides advance to the next rolling key.
    if (!sendAck(payload.sequenceNumber, nextKey, currentKey)) return;
    memcpy(rollingKey, nextKey, AES_KEY_SIZE);

    lastAcceptedSequence = payload.sequenceNumber;
    lastAcceptedCommandMs = millis();
    lastReceivedPacketMs = lastAcceptedCommandMs;
    sonarDistanceCm = payload.sonarDistance;

    if (payload.isOwner != 1) {
        safeNeutral();
        return;
    }
    appliedSteering = constrain(payload.steering, 0, 180);
    appliedThrottle = constrain(payload.throttle, 0, 180);
    steeringServo.write(appliedSteering);
    throttleESC.write(appliedThrottle);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(SONAR_TRIG_PIN, OUTPUT);
    pinMode(SONAR_ECHO_PIN, INPUT);

    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    steeringServo.attach(SERVO_PIN, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
    throttleESC.attach(ESC_PIN, ESC_MIN_PULSE, ESC_MAX_PULSE);
    safeNeutral();

    mrp.deriveKey(MRP_MASTER_SEED, 0, rollingKey);
    WiFi.mode(WIFI_STA);
    Serial.printf("[VehicleUnit] STA MAC: %s\n", WiFi.macAddress().c_str());
    if (!validControllerMacConfigured()) {
        Serial.println("[FATAL] Set controllerUnitAddress to the WROVER STA MAC in Config.h");
        return;
    }
    if (esp_now_init() != ESP_OK) {
        Serial.println("[FATAL] esp_now_init failed");
        return;
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, controllerUnitAddress, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[FATAL] esp_now_add_peer failed");
        return;
    }
    esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));
    Serial.println("[READY] ESP32-S3 Vehicle Unit: encrypted Minas MRP actuator");
}

void loop() {
    const uint32_t now = millis();
    if (lastAcceptedCommandMs == 0 ||
        now - lastAcceptedCommandMs > COMMAND_TIMEOUT_MS) {
        safeNeutral();
    }
    if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
        lastTelemetryMs = now;
        sonarDistanceCm = readSonarCm();
    }
    delay(1);
}
