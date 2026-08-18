#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ps5Controller.h>
#include "Config.h"
#include "DriverModel.h"
#include "MLGate.h"
#include "../../shared/MRP.h"

namespace {
MRPProtocol mrp;
MovingWindowGate mlGate;
bool radioReady = false;
bool controllerReady = false;
bool sdReady = false;
bool awaitingAck = false;
bool radioBlocked = false;
bool ownerLabel = INITIAL_OWNER_LABEL != 0;
bool previousCircle = false;
uint32_t inputSequence = 0;
uint32_t commandSequence = 0;
uint32_t trialNumber = 0;
uint32_t lastSampleMs = 0;
uint32_t lastLoopSampleMs = 0;
uint32_t pendingSequence = 0;
uint32_t pendingSentMs = 0;
uint8_t rollingKey[AES_KEY_SIZE] = {};
uint32_t lastAckedCounter = 0;
uint32_t lastAckEventMs = 0;
uint32_t lastAckEventSequence = 0;
uint32_t ackTimeoutCount = 0;
uint32_t sendFailureCount = 0;
uint32_t ackCount = 0;
uint32_t droppedCount = 0;
uint32_t lastAckAgeMs = 0;
uint32_t lastSequenceGap = 0;
uint32_t trialSampleCount = 0;
uint32_t trialStartMs = 0;
int previousSteering = 90;
int previousThrottle = ESC_NEUTRAL_ANGLE;
int latestSonarDistance = 999;
String activeFilePath;
File activeFile;

bool validMacConfigured() {
    for (uint8_t byte : vehicleUnitAddress) if (byte != 0x00) return true;
    return false;
}

void closeTrial() {
    if (activeFile) {
        activeFile.printf("TRAILER,samples=%lu,dropped=%lu,ack_timeouts=%lu,send_failures=%lu,acks=%lu\n",
            static_cast<unsigned long>(trialSampleCount),
            static_cast<unsigned long>(droppedCount),
            static_cast<unsigned long>(ackTimeoutCount),
            static_cast<unsigned long>(sendFailureCount),
            static_cast<unsigned long>(ackCount));
        activeFile.flush();
        activeFile.close();
    }
    trialSampleCount = 0;
}

bool openTrialFile() {
    if (!sdReady) return false;
    closeTrial();
    const char* label = ownerLabel ? "owner" : "nonowner";
    do {
        ++trialNumber;
        activeFilePath = String(SD_LOG_PREFIX) + String(trialNumber) + "_" + label + ".csv";
    } while (SD.exists(activeFilePath));
    activeFile = SD.open(activeFilePath, FILE_WRITE);
    if (!activeFile) return false;
    trialStartMs = millis();
    activeFile.println("schema_version=4");
    activeFile.printf("session_id=%s\n", MINAS_SESSION_ID);
    activeFile.printf("driver_id=%s\n", MINAS_DRIVER_ID);
    activeFile.printf("route_id=%s\n", MINAS_ROUTE_ID);
    activeFile.printf("label=%s\n", label);
    activeFile.printf("firmware=%s\n", MINAS_FW_VERSION);
    activeFile.printf("sampling_target_ms=%u\n", TELEMETRY_INTERVAL_MS);
    activeFile.printf("operation_mode=%d\n", CONTROLLER_OPERATION_MODE);
    activeFile.println("---");
    activeFile.println("sample_id,controller_ms,actual_dt_ms,steering,throttle,l2,r2,buttons_mask,ground_truth_owner,ml_ready,ml_decision,confidence_permille,allow_motion,sent,ack_received,ack_age_ms,sequence_gap,ack_timeout,send_failure,radio_blocked,ps5_connected");
    activeFile.flush();
    return true;
}

void writeSample(uint32_t now, uint32_t actualDtMs, int steering, int throttle,
                 int l2, int r2, uint16_t buttonsMask, const MLDecision& ml,
                 bool allow, bool sent, bool ackReceived, bool ackTimeout,
                 bool sendFailure, uint32_t sequenceGap) {
    if (!activeFile) return;
    activeFile.printf("%lu,%lu,%lu,%d,%d,%d,%d,%u,%d,%d,%d,%u,%d,%d,%d,%lu,%lu,%d,%d,%d,%d\n",
        static_cast<unsigned long>(++trialSampleCount),
        static_cast<unsigned long>(now),
        static_cast<unsigned long>(actualDtMs),
        steering, throttle, l2, r2,
        static_cast<unsigned>(buttonsMask),
        ownerLabel ? 1 : 0,
        ml.ready ? 1 : 0,
        ml.decision ? 1 : 0,
        static_cast<unsigned>(ml.confidence),
        allow ? 1 : 0,
        sent ? 1 : 0,
        ackReceived ? 1 : 0,
        static_cast<unsigned long>(lastAckAgeMs),
        static_cast<unsigned long>(sequenceGap),
        ackTimeout ? 1 : 0,
        sendFailure ? 1 : 0,
        radioBlocked ? 1 : 0,
        ps5.isConnected() ? 1 : 0);
    if ((trialSampleCount % 10U) == 0U) activeFile.flush();
}

bool sendMrpPayload(const telemetry_payload_t& payload) {
    if (!radioReady || radioBlocked || awaitingAck) return false;
    uint8_t ciphertext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    size_t ciphertextLength = 0;
    if (!mrp.encrypt(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload),
                     ciphertext, rollingKey, ciphertextLength)) return false;
    const esp_err_t result = esp_now_send(vehicleUnitAddress, ciphertext, ciphertextLength);
    if (result != ESP_OK) return false;
    awaitingAck = true;
    pendingSequence = payload.sequenceNumber;
    pendingSentMs = millis();
    return true;
}

void handleAckTimeout(uint32_t now) {
    if (!awaitingAck || now - pendingSentMs <= COMMAND_ACK_TIMEOUT_MS) return;
    awaitingAck = false;
    radioBlocked = true;
    ++ackTimeoutCount;
    ++droppedCount;
    Serial.println("[MRP] ACK timeout; radio blocked until reboot to avoid key desynchronization");
}

void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (mac == nullptr || data == nullptr || len <= 0 ||
        memcmp(mac, vehicleUnitAddress, 6) != 0 ||
        len != static_cast<int>(mrpPaddedLength(sizeof(ack_payload_t)))) return;
    uint8_t plaintext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    if (!mrp.decrypt(data, static_cast<size_t>(len), plaintext, rollingKey)) return;
    ack_payload_t ack{};
    memcpy(&ack, plaintext, sizeof(ack));
    if (ack.magic != MAGIC_NUMBER || !awaitingAck || ack.receiverCounter != pendingSequence) return;
    if (ack.receiverCounter <= lastAckedCounter) return;
    memcpy(rollingKey, ack.newKey, AES_KEY_SIZE);
    lastAckedCounter = ack.receiverCounter;
    awaitingAck = false;
    lastAckAgeMs = millis() - pendingSentMs;
    lastAckEventMs = millis();
    lastAckEventSequence = ack.receiverCounter;
    ++ackCount;
}

uint16_t buildButtonsMask() {
    uint16_t mask = 0;
    if (ps5.Cross()) mask |= 1u << 0;
    if (ps5.Circle()) mask |= 1u << 1;
    if (ps5.Square()) mask |= 1u << 2;
    if (ps5.Triangle()) mask |= 1u << 3;
    if (ps5.L1()) mask |= 1u << 4;
    if (ps5.R1()) mask |= 1u << 5;
    return mask;
}

void processController() {
    const uint32_t now = millis();
    handleAckTimeout(now);
    if (!ps5.isConnected()) return;

    const bool circle = ps5.Circle();
    if (circle && !previousCircle) {
        ownerLabel = !ownerLabel;
        mlGate.reset();
        openTrialFile();
        Serial.printf("[LABEL] %s\n", ownerLabel ? "owner" : "nonowner");
    }
    previousCircle = circle;

    const uint32_t actualDtMs = lastSampleMs == 0 ? TELEMETRY_INTERVAL_MS : now - lastSampleMs;
    lastSampleMs = now;
    const int steering = constrain(map(ps5.LStickX(), -128, 127, 0, 180), 0, 180);
    const int throttle = constrain(map(ps5.RStickY(), -128, 127, 180, 0), 0, 180);
    const int l2 = ps5.L2Value();
    const int r2 = ps5.R2Value();
    const uint16_t buttonsMask = buildButtonsMask();
    const float dtSeconds = actualDtMs > 0 ? actualDtMs / 1000.0f : 0.05f;

    mlGate.push(steering / 180.0f, throttle / 180.0f, dtSeconds);
    const MLDecision ml = mlGate.predict();
    bool allow = true;
    if (CONTROLLER_OPERATION_MODE == 3) {
        allow = ml.valid && ml.ready && ml.decision && ml.confidence >= ML_MIN_CONFIDENCE_PERMILLE;
    }

    const bool ackTimeout = radioBlocked;
    bool sendFailure = false;
    bool sent = false;
    bool ackReceived = false;
    uint32_t sequenceGap = 0;

    telemetry_payload_t payload{};
    payload.sequenceNumber = ++commandSequence;
    payload.timestamp = now;
    payload.throttle = allow ? throttle : ESC_NEUTRAL_ANGLE;
    payload.steering = steering;
    payload.sonarDistance = latestSonarDistance;
    // This legacy field is now a coarse radio-health flag; detailed status is in CSV.
    payload.packetLost = (radioBlocked || awaitingAck) ? 1 : 0;
    payload.isOwner = allow ? 1 : 0;
    payload.deltaTime = actualDtMs;
    payload.steerVelocity = (steering - previousSteering) / dtSeconds;
    payload.throttleVelocity = (throttle - previousThrottle) / dtSeconds;
    payload.magic = MAGIC_NUMBER;
    previousSteering = steering;
    previousThrottle = throttle;

    const bool canAttemptSend = radioReady && !radioBlocked && !awaitingAck;
    if (canAttemptSend) {
        sent = sendMrpPayload(payload);
        sendFailure = !sent;
    }
    if (sendFailure) {
        ++sendFailureCount;
        ++droppedCount;
    }
    ackReceived = (lastAckEventSequence == payload.sequenceNumber);
    writeSample(now, actualDtMs, steering, throttle, l2, r2, buttonsMask, ml,
                allow, sent, ackReceived, ackTimeout, sendFailure, sequenceGap);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    WiFi.mode(WIFI_STA);
    Serial.printf("[ControllerUnit] STA MAC: %s\n", WiFi.macAddress().c_str());
    mrp.deriveKey(MRP_MASTER_SEED, 0, rollingKey);

    if (!validMacConfigured()) {
        Serial.println("[WARN] vehicleUnitAddress is not set; local PS5 + SD collection mode");
    } else if (esp_now_init() != ESP_OK) {
        Serial.println("[WARN] esp_now_init failed; continuing without Vehicle Unit");
    } else {
        esp_now_peer_info_t peer{};
        memcpy(peer.peer_addr, vehicleUnitAddress, 6);
        peer.channel = 0;
        peer.encrypt = false;
        if (esp_now_add_peer(&peer) != ESP_OK) {
            Serial.println("[WARN] esp_now_add_peer failed; continuing without Vehicle Unit");
        } else {
            esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));
            radioReady = true;
        }
    }

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdReady = SD.begin(SD_CS_PIN, SPI);
    if (!sdReady) Serial.println("[WARN] SD unavailable; samples will not be saved");
    if (sdReady) openTrialFile();

    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("[FATAL] PS5 Bluetooth initialization failed");
        return;
    }
    controllerReady = true;
    Serial.printf("[READY] PS5 + SD + local ML; radio=%s model=%s mode=%d\n",
                  radioReady ? "enabled" : "disabled",
                  MINAS_MODEL_READY ? "ready" : "missing",
                  CONTROLLER_OPERATION_MODE);
}

void loop() {
    if (!controllerReady) {
        delay(20);
        return;
    }
    const uint32_t now = millis();
    handleAckTimeout(now);
    if (now - lastLoopSampleMs >= TELEMETRY_INTERVAL_MS) {
        lastLoopSampleMs = now;
        processController();
    }
    delay(1);
}
