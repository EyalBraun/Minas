#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_now.h>
#include <ps5Controller.h>
#include "Config.h"
#include "../../shared/MinasProtocol.h"

namespace {
bool radioReady = false;
bool controllerReady = false;
bool sdReady = false;
bool ownerLabel = INITIAL_OWNER_LABEL != 0;
bool previousCircle = false;
uint32_t inputSequence = 0;
uint32_t commandSequence = 0;
uint32_t trialNumber = 0;
uint32_t lastSendMs = 0;
uint32_t lastVehicleTelemetryMs = 0;
String activeFilePath;
File activeFile;
VehicleTelemetry latestVehicleTelemetry{};

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
    } while (SD.exists(activeFilePath));
    activeFile = SD.open(activeFilePath, FILE_WRITE);
    if (!activeFile) return false;
    activeFile.println("schema_version=2");
    activeFile.printf("label=%s\n", label);
    activeFile.printf("operation_mode=%d\n", CONTROLLER_OPERATION_MODE);
    activeFile.println("---");
    activeFile.println("trial_number,input_sequence,timestamp_ms,steering,throttle,l2,r2,buttons_mask,owner_label,ml_decision,confidence_permille,allow_motion,vehicle_sonar_cm,vehicle_failsafe");
    activeFile.flush();
    Serial.printf("[SD] Opened %s\n", activeFilePath.c_str());
    return true;
}

void writeSample(uint32_t seq, uint32_t now, int steering, int throttle,
                 int l2, int r2, uint16_t buttonsMask,
                 MinasDecision decision, uint16_t confidence, bool allow) {
    if (!activeFile) return;
    activeFile.printf("%lu,%lu,%lu,%d,%d,%d,%d,%u,%d,%u,%u,%d,%d,%d\n",
        static_cast<unsigned long>(trialNumber), static_cast<unsigned long>(seq),
        static_cast<unsigned long>(now), steering, throttle, l2, r2,
        static_cast<unsigned>(buttonsMask), ownerLabel ? 1 : 0,
        static_cast<unsigned>(decision), static_cast<unsigned>(confidence),
        allow ? 1 : 0, latestVehicleTelemetry.sonarDistanceCm,
        latestVehicleTelemetry.failsafeActive ? 1 : 0);
    static uint8_t flushCounter = 0;
    if (++flushCounter >= 10) {
        activeFile.flush();
        flushCounter = 0;
    }
}

// Replace with the trained classifier later. Mode 1 deliberately bypasses ML
// for data collection; mode 2 logs the result but continues; mode 3 enforces it.
MinasDecision classifyDriver(bool owner, uint16_t& confidence) {
#if CONTROLLER_OPERATION_MODE == 1
    confidence = 1000;
    return MINAS_DECISION_ALLOW_OWNER;
#elif CONTROLLER_OPERATION_MODE == 2
    confidence = owner ? 1000 : 0;
    return owner ? MINAS_DECISION_ALLOW_OWNER : MINAS_DECISION_STOP;
#else
    confidence = owner ? 1000 : 0;
    return owner ? MINAS_DECISION_ALLOW_OWNER : MINAS_DECISION_STOP;
#endif
}

void sendCommand(int steering, int throttle, MinasDecision decision,
                 uint16_t confidence, uint32_t now) {
    AuthorizedVehicleCommand command{};
    command.magic = MINAS_PROTOCOL_MAGIC;
    command.version = MINAS_PROTOCOL_VERSION;
    command.messageType = MINAS_MSG_AUTHORIZED_COMMAND;
    command.decision = decision;
    command.allowMotion = decision == MINAS_DECISION_ALLOW_OWNER ? 1 : 0;
    command.sequence = ++commandSequence;
    command.inputSequence = inputSequence;
    command.issuedAtMs = now;
    command.expiresAtMs = now + COMMAND_TTL_MS;
    command.steering = constrain(steering, 0, 180);
    command.throttle = command.allowMotion ? constrain(throttle, 0, 180) : ESC_NEUTRAL_ANGLE;
    command.confidencePermille = confidence;
    minasFinalize(command);
    const esp_err_t result = esp_now_send(vehicleUnitAddress,
        reinterpret_cast<const uint8_t*>(&command), sizeof(command));
    if (result != ESP_OK) Serial.printf("[RADIO] send failed: %d\n", result);
}

void onDataRecv(const uint8_t*, const uint8_t* data, int len) {
    if (len != static_cast<int>(sizeof(VehicleTelemetry))) return;
    VehicleTelemetry packet{};
    memcpy(&packet, data, sizeof(packet));
    if (!minasValidate(packet) || packet.messageType != MINAS_MSG_VEHICLE_TELEMETRY) return;
    latestVehicleTelemetry = packet;
    lastVehicleTelemetryMs = millis();
}

void processController() {
    const uint32_t now = millis();
    if (!ps5.isConnected()) {
        sendCommand(90, ESC_NEUTRAL_ANGLE, MINAS_DECISION_STOP, 0, now);
        return;
    }

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
    const MinasDecision decision = classifyDriver(ownerLabel, confidence);
    const bool allow = decision == MINAS_DECISION_ALLOW_OWNER;
    sendCommand(steering, throttle, decision, confidence, now);
    writeSample(inputSequence, now, steering, throttle, l2, r2, buttonsMask, decision, confidence, allow);
}
} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    WiFi.mode(WIFI_STA);
    Serial.printf("[ControllerUnit] STA MAC: %s\n", WiFi.macAddress().c_str());
    if (!validMacConfigured()) {
        Serial.println("[FATAL] Set vehicleUnitAddress to the S3 Vehicle Unit STA MAC in Config.h");
        return;
    }
    if (esp_now_init() != ESP_OK) {
        Serial.println("[FATAL] esp_now_init failed");
        return;
    }
    esp_now_peer_info_t peer{};
    memcpy(peer.peer_addr, vehicleUnitAddress, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("[FATAL] esp_now_add_peer failed");
        return;
    }
    esp_now_register_recv_cb(esp_now_recv_cb_t(onDataRecv));
    radioReady = true;

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdReady = SD.begin(SD_CS_PIN, SPI);
    if (!sdReady) Serial.println("[WARN] SD unavailable; samples will not be saved");
    if (sdReady) openTrialFile();

    ps5.begin(PS5_CONTROLLER_MAC);
    controllerReady = true;
    Serial.println("[READY] WROVER Controller Unit: PS5 + SD + authorized-command sender");
}

void loop() {
    if (!radioReady || !controllerReady) {
        delay(20);
        return;
    }
    if (millis() - lastSendMs >= TELEMETRY_INTERVAL_MS) {
        lastSendMs = millis();
        processController();
    }
    delay(1);
}
