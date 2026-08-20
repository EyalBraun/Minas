#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ps5Controller.h>
#include "Config.h"
#include "../../shared/MRP.h"

namespace {
MRPProtocol mrp;
bool radioReady = false;
bool controllerReady = false;
bool sdReady = false;
bool awaitingAck = false;
bool ackTimeoutReported = false;
bool linkActive = false;
bool ownerLabel = INITIAL_OWNER_LABEL != 0;
bool previousCircle = false;
uint32_t inputSequence = 0;
uint32_t commandSequence = 0;
uint32_t trialNumber = 0;
uint32_t lastSendMs = 0;
uint32_t lastAckMs = 0;
uint32_t lastVehicleTelemetryMs = 0;
uint32_t lastAckedCounter = 0;
uint8_t rollingKey[AES_KEY_SIZE] = {};
String activeFilePath;
File activeFile;
int latestSonarDistance = 999;

bool validMacConfigured() {
    for (uint8_t byte : vehicleUnitAddress) if (byte != 0x00) return true;
    return false;
}

void closeTrial() {
    if (activeFile) {
        activeFile.flush();
        activeFile.close();
    }
}

bool openTrialFile() {
    if (!sdReady) return false;
    closeTrial();
    const char* label = ownerLabel ? "owner" : "nonowner";
    do {
        ++trialNumber;
        activeFilePath = String(SD_LOG_PREFIX) + String(trialNumber) + "_" + label + ".csv";
    } while (SD_MMC.exists(activeFilePath));
    activeFile = SD_MMC.open(activeFilePath, FILE_WRITE);
    if (!activeFile) return false;
    activeFile.println("schema_version=3");
    activeFile.println("transport=Minas_MRP_AES_ECB_rolling_key");
    activeFile.printf("label=%s\n", label);
    activeFile.printf("operation_mode=%d\n", CONTROLLER_OPERATION_MODE);
    activeFile.println("---");
    activeFile.println("trial_number,input_sequence,timestamp_ms,steering,throttle,l2,r2,buttons_mask,owner_label,ml_decision,confidence_permille,allow_motion,vehicle_sonar_cm,vehicle_failsafe");
    activeFile.flush();
    Serial.printf("[SD] Opened %s\n", activeFilePath.c_str());
    return true;
}

void writeSample(uint32_t seq, uint32_t now, int steering, int throttle,
                 int l2, int r2, uint16_t buttonsMask, uint8_t decision,
                 uint16_t confidence, bool allow) {
    if (!activeFile) return;
    const size_t written = activeFile.printf(
        "%lu,%lu,%lu,%d,%d,%d,%d,%u,%d,%u,%u,%d,%d,%d\n",
        static_cast<unsigned long>(trialNumber),
        static_cast<unsigned long>(seq),
        static_cast<unsigned long>(now),
        steering,
        throttle,
        l2,
        r2,
        static_cast<unsigned>(buttonsMask),
        ownerLabel ? 1 : 0,
        static_cast<unsigned>(decision),
        static_cast<unsigned>(confidence),
        allow ? 1 : 0,
        latestSonarDistance,
        awaitingAck ? 1 : 0
    );

    if (written == 0) {
        Serial.println("[SD] Write failed; closing active file");
        activeFile.flush();
        activeFile.close();
        sdReady = false;
        return;
    }

    // Flush every sample while validating the card and filesystem.
    activeFile.flush();
}

uint8_t classifyDriver(bool owner, uint16_t& confidence) {
#if CONTROLLER_OPERATION_MODE == 1
    confidence = 1000;
    return DRIVE_CONTINUE;
#elif CONTROLLER_OPERATION_MODE == 2
    confidence = owner ? 1000 : 0;
    return owner ? DRIVE_CONTINUE : DRIVE_STOP;
#else
    confidence = owner ? 1000 : 0;
    return owner ? DRIVE_CONTINUE : DRIVE_STOP;
#endif
}

bool sendMrpPayload(const telemetry_payload_t& payload) {
    if (!radioReady || awaitingAck) return false;

    uint8_t ciphertext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    size_t ciphertextLength = 0;
    if (!mrp.encrypt(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload),
                     ciphertext, rollingKey, ciphertextLength)) {
        Serial.println("[MRP] telemetry encryption failed");
        return false;
    }

    const esp_err_t result = esp_now_send(vehicleUnitAddress, ciphertext,
                                          ciphertextLength);
    if (result != ESP_OK) {
        Serial.printf("[MRP] send failed: %d\n", result);
        return false;
    }
    awaitingAck = true;
    ackTimeoutReported = false;
    Serial.printf("[MRP] TX command seq=%lu; waiting for ACK\n",
                  static_cast<unsigned long>(payload.sequenceNumber));
    return true;
}

void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (mac == nullptr || data == nullptr || len <= 0 ||
        memcmp(mac, vehicleUnitAddress, 6) != 0 ||
        len != static_cast<int>(mrpPaddedLength(sizeof(ack_payload_t)))) return;

    uint8_t plaintext[MRP_MAX_CIPHERTEXT_SIZE] = {};
    if (!mrp.decrypt(data, static_cast<size_t>(len), plaintext, rollingKey)) return;

    ack_payload_t ack{};
    memcpy(&ack, plaintext, sizeof(ack));
    if (ack.magic != MAGIC_NUMBER || ack.receiverCounter < lastAckedCounter) return;

    memcpy(rollingKey, ack.newKey, AES_KEY_SIZE);
    lastAckedCounter = ack.receiverCounter;
    awaitingAck = false;
    ackTimeoutReported = false;
    lastAckMs = millis();
    if (!linkActive) {
        Serial.println("[LINK] Controller <-> Vehicle communication established");
    }
    linkActive = true;
    Serial.printf("[MRP] RX ACK counter=%lu; link is active\n",
                  static_cast<unsigned long>(ack.receiverCounter));
}

void processController() {
    const uint32_t now = millis();
    if (!ps5.isConnected()) return;

    const bool circle = ps5.Circle();
    if (circle && !previousCircle) {
        ownerLabel = !ownerLabel;
        openTrialFile();
        Serial.printf("[LABEL] %s\n", ownerLabel ? "owner" : "nonowner");
    }
    previousCircle = circle;

    const int steering = map(ps5.LStickX(), -128, 127, 0, 180);
    const int throttle = map(ps5.RStickY(), -128, 127, 180, 0);
    const int l2 = ps5.L2Value();
    const int r2 = ps5.R2Value();
    uint16_t buttonsMask = 0;
    if (ps5.Cross()) buttonsMask |= 1u << 0;
    if (ps5.Circle()) buttonsMask |= 1u << 1;
    if (ps5.Square()) buttonsMask |= 1u << 2;
    if (ps5.Triangle()) buttonsMask |= 1u << 3;
    if (ps5.L1()) buttonsMask |= 1u << 4;
    if (ps5.R1()) buttonsMask |= 1u << 5;
    ++inputSequence;

    uint16_t confidence = 0;
    const uint8_t decision = classifyDriver(ownerLabel, confidence);
    const bool allow = decision == DRIVE_CONTINUE;

    telemetry_payload_t payload{};
    payload.sequenceNumber = ++commandSequence;
    payload.timestamp = now;
    payload.steering = constrain(steering, 0, 180);
    payload.throttle = allow ? constrain(throttle, 0, 180) : ESC_NEUTRAL_ANGLE;
    payload.sonarDistance = latestSonarDistance;
    payload.packetLost = awaitingAck ? 1 : 0;
    // In collection mode both owner and non-owner trials may drive. In an
    // enforcement mode this field becomes the classifier's authorization.
    payload.isOwner = allow ? 1 : 0;
    payload.deltaTime = TELEMETRY_INTERVAL_MS;
    payload.steerVelocity = 0.0f;
    payload.throttleVelocity = 0.0f;
    payload.magic = MAGIC_NUMBER;

    sendMrpPayload(payload);
    writeSample(inputSequence, now, steering, throttle, l2, r2, buttonsMask,
                decision, confidence, allow);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    WiFi.mode(WIFI_STA);
    // Keep the original Bluetooth Classic initialization order. The PS5
    // controller library must start without forcing Wi-Fi coexistence settings.
    Serial.printf("[ControllerUnit] STA MAC: %s\n", WiFi.macAddress().c_str());
    mrp.deriveKey(MRP_MASTER_SEED, 0, rollingKey);

    if (!validMacConfigured()) {
        Serial.println("[WARN] vehicleUnitAddress is not set; PS5/SD local test mode");
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
            Serial.println("[LINK] ESP-NOW peer configured; waiting for Vehicle ACK");
        }
    }

    // The onboard slot on this WROVER works in 1-bit SDMMC mode.
    sdReady = SD_MMC.begin("/sdcard", true, false);
    if (!sdReady) Serial.println("[WARN] SD unavailable; samples will not be saved");
    if (sdReady) openTrialFile();

    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("[FATAL] PS5 Bluetooth initialization failed");
        return;
    }

    // Set the same ESP-NOW channel as the Vehicle only after Bluetooth
    // Classic has started. Setting it before ps5.begin() caused a BT
    // coexistence abort on this WROVER build.
    const esp_err_t channelResult = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    Serial.printf("[WiFi] ESP-NOW channel=%d set_result=%d\n", ESPNOW_CHANNEL, channelResult);

    controllerReady = true;
    Serial.println("[PS5] Bluetooth stack initialized; waiting for DualSense connection");
    Serial.printf("[READY] Controller Unit: PS5 + SD + MRP; ESP-NOW=%s\n",
                  radioReady ? "enabled" : "waiting for Vehicle Unit MAC");
}

void loop() {
    if (awaitingAck && !ackTimeoutReported &&
        millis() - lastSendMs > COMMAND_TTL_MS) {
        ackTimeoutReported = true;
        linkActive = false;
        Serial.println("[LINK] ACK timeout; Vehicle communication not confirmed");
        Serial.println("[LINK] Radio remains blocked to protect MRP key synchronization");
    }

    if (!controllerReady) {
        delay(20);
        return;
    }
    if (millis() - lastSendMs >= TELEMETRY_INTERVAL_MS) {
        lastSendMs = millis();
        processController();
    }
    delay(1);
}
