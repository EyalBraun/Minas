#include <Arduino.h>
#include <ps5Controller.h>
#include <ESP32Servo.h>
#include "Config.h"
#include "Buzzer.h"
#include "DataLogger.h"

Servo steeringServo;
Servo throttleESC;
DataLogger logger;

unsigned long lastRecordTime = 0;
unsigned long lastStatusBeepTime = 0;
unsigned long lastSonarCheckTime = 0;
unsigned long lastObstacleBeepTime = 0;
bool wasConnected = false;
int frontDistanceCm = 999;

int getSonarDistanceCM() {
    digitalWrite(SONAR_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SONAR_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SONAR_TRIG_PIN, LOW);

    const unsigned long duration = pulseIn(SONAR_ECHO_PIN, HIGH, SONAR_TIMEOUT_US);
    if (duration == 0) {
        return 999; // No echo: obstacle is out of the selected measurement range.
    }

    return static_cast<int>((duration * 0.0343f) / 2.0f);
}

void updateFrontSonar() {
    if (millis() - lastSonarCheckTime < SONAR_INTERVAL_MS) {
        return;
    }

    lastSonarCheckTime = millis();
    frontDistanceCm = getSonarDistanceCM();
}

void setup() {
    Serial.begin(115200);

    pinMode(SONAR_TRIG_PIN, OUTPUT);
    pinMode(SONAR_ECHO_PIN, INPUT);
    digitalWrite(SONAR_TRIG_PIN, LOW);

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

    // Keep the ESC at neutral while it initializes.
    throttleESC.write(ESC_NEUTRAL_ANGLE);
    delay(2000);
}

void handleInput() {
    const int steerAngle = map(ps5.LStickX(), -128, 127, 0, 180);
    int throttleValue = map(ps5.RStickY(), -128, 127, 180, 0);

    updateFrontSonar();

    // A front sensor must block forward movement, but reverse remains available
    // so the car can move away from the detected obstacle.
    const bool isForwardCommand = throttleValue > ESC_NEUTRAL_ANGLE;
    const bool obstacleTooClose = frontDistanceCm <= OBSTACLE_DISTANCE_CM;
    if (isForwardCommand && obstacleTooClose) {
        throttleValue = ESC_NEUTRAL_ANGLE;

        if (millis() - lastObstacleBeepTime >= OBSTACLE_BEEP_INTERVAL_MS) {
            playObstacleSound();
            lastObstacleBeepTime = millis();
        }

        Serial.printf("[SAFETY] Front obstacle at %d cm. Forward motion blocked.\n", frontDistanceCm);
    }

    steeringServo.write(steerAngle);
    throttleESC.write(throttleValue);

    // Toggle Owner/Guest mode with the Circle button.
    static bool circlePressed = false;
    if (ps5.Circle()) {
        if (!circlePressed) {
            const bool currentMode = logger.isOwnerMode();
            logger.setOwnerMode(!currentMode);
            Serial.printf("Mode Changed: %s\n", !currentMode ? "OWNER" : "GUEST");
            circlePressed = true;
        }
    } else {
        circlePressed = false;
    }

    // The Cross (X) button deliberately has no action and must not add a log sample.
    const bool suppressLogWhileCrossPressed = ps5.Cross();
    if (suppressLogWhileCrossPressed) {
        // Intentionally empty.
    }

    if (!suppressLogWhileCrossPressed && millis() - lastRecordTime >= RECORD_INTERVAL_MS) {
        logger.sample(steerAngle, throttleValue);
        lastRecordTime = millis();
    }
}

void loop() {
    const bool isConnectedNow = ps5.isConnected();

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

        if (millis() - lastStatusBeepTime >= BEEP_INTERVAL_MS) {
            if (logger.isOwnerMode()) {
                playOwnerModeBeep();
            } else {
                playGuestModeBeep();
            }
            lastStatusBeepTime = millis();
        }
    } else {
        // Bluetooth fail-safe: stop in place; no rewind or return-to-home action.
        steeringServo.write(ESC_NEUTRAL_ANGLE);
        throttleESC.write(ESC_NEUTRAL_ANGLE);
    }

    logger.update(LOG_INTERVAL_MS);
}
