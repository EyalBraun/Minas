#include <Arduino.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <esp_now.h>
#include "Config.h"
#include "../../shared/MinasProtocol.h"

Servo steeringServo;
Servo throttleESC;
volatile bool commandPending = false;
volatile uint32_t pendingReceivedMs = 0;
AuthorizedVehicleCommand pendingCommand{};
uint32_t lastAcceptedSequence = 0;
uint32_t lastAcceptedCommandMs = 0;
uint32_t telemetrySequence = 0;
uint32_t lastTelemetryMs = 0;
int appliedSteering = 90;
int appliedThrottle = ESC_NEUTRAL_ANGLE;
int sonarDistanceCm = 999;

bool validDtMacConfigured() {
    for (uint8_t byte : controllerUnitAddress) if (byte != 0x00) return true;
    return false;
}

void safeNeutral() {
    // Fail-safe state: center steering and put the ESC at neutral.
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

void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (mac == nullptr || data == nullptr || len != static_cast<int>(sizeof(AuthorizedVehicleCommand))) return;
    if (memcmp(mac, controllerUnitAddress, 6) != 0) return;
    AuthorizedVehicleCommand command{};
    memcpy(&command, data, sizeof(command));
    if (!minasValidate(command) || command.messageType != MINAS_MSG_AUTHORIZED_COMMAND) return;
    if (command.sequence <= lastAcceptedSequence) return;
    pendingCommand = command;
    pendingReceivedMs = millis();
    commandPending = true;
}

void applyPendingCommand() {
    if (!commandPending) return;
    noInterrupts();
    AuthorizedVehicleCommand command{};
    memcpy(&command, &pendingCommand, sizeof(command));
    const uint32_t receivedMs = pendingReceivedMs;
    commandPending = false;
    interrupts();

    const uint32_t now = millis();
    if (command.sequence <= lastAcceptedSequence) return;
    // Do not compare DT and DR millis() values. They have independent clocks.
    // The two boards have independent millis() clocks. The DR validates
    // freshness using the local receive time, not command.issuedAtMs.
    if (now - receivedMs > COMMAND_TIMEOUT_MS) {
        safeNeutral();
        return;
    }
    if (command.allowMotion != 1 || command.decision != MINAS_DECISION_ALLOW_OWNER) {
        safeNeutral();
        lastAcceptedSequence = command.sequence;
        lastAcceptedCommandMs = now;
        return;
    }

    appliedSteering = constrain(command.steering, 0, 180);
    appliedThrottle = constrain(command.throttle, 0, 180);
    steeringServo.write(appliedSteering);
    throttleESC.write(appliedThrottle);
    lastAcceptedSequence = command.sequence;
    lastAcceptedCommandMs = now;
}

void sendTelemetry() {
    const uint32_t now = millis();
    if (now - lastTelemetryMs < TELEMETRY_INTERVAL_MS) return;
    lastTelemetryMs = now;
    VehicleTelemetry packet{};
    packet.magic = MINAS_PROTOCOL_MAGIC;
    packet.version = MINAS_PROTOCOL_VERSION;
    packet.messageType = MINAS_MSG_VEHICLE_TELEMETRY;
    packet.sequence = ++telemetrySequence;
    packet.timestampMs = now;
    packet.steeringApplied = appliedSteering;
    packet.throttleApplied = appliedThrottle;
    packet.sonarDistanceCm = sonarDistanceCm;
    packet.packetLoss = 0;
    packet.failsafeActive = (lastAcceptedCommandMs == 0 ||
        now - lastAcceptedCommandMs > COMMAND_TIMEOUT_MS) ? 1 : 0;
    minasFinalize(packet);
    esp_now_send(controllerUnitAddress, reinterpret_cast<const uint8_t*>(&packet), sizeof(packet));
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
    steeringServo.write(90);
    safeNeutral();

    WiFi.mode(WIFI_STA);
    Serial.printf("[VehicleUnit] STA MAC: %s\n", WiFi.macAddress().c_str());
    if (!validDtMacConfigured()) {
        Serial.println("[FATAL] Set controllerUnitAddress to the WROVER Controller Unit STA MAC in Config.h");
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
    Serial.println("[READY] ESP32-S3 Vehicle Unit: authorized-command vehicle actuator");
}

void loop() {
    applyPendingCommand();
    const uint32_t now = millis();
    if (lastAcceptedCommandMs == 0 || now - lastAcceptedCommandMs > COMMAND_TIMEOUT_MS) safeNeutral();
    if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
        sonarDistanceCm = readSonarCm();
        sendTelemetry();
    }
    delay(1);
}
