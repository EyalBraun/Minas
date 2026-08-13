#include <Arduino.h>
#include <Buzzer.h>
#include <Config.h>

#define BUZZER_LEDC_CHANNEL 7

void initBuzzer() {
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

void playTone(int frequency, int durationMs) {
    if (frequency > 0) {
        ledcSetup(BUZZER_LEDC_CHANNEL, frequency, 8);
        ledcWrite(BUZZER_LEDC_CHANNEL, 127);
    } else {
        ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    }
    delay(durationMs);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0);
}

void playConnectSound() {
    playTone(523, 80); delay(50);
    playTone(659, 80); delay(50);
    playTone(784, 100);
}

void playDisconnectSound() {
    playTone(784, 100); delay(50);
    playTone(523, 150);
}

void playObstacleSound() {
    // Rapid urgent beeps when obstacle detected (< 30cm)
    playTone(2200, 80);
    delay(50);
}

void playMuteWarningSound() {
    playTone(2000, 60);
}

// Periodic status sounds
void playOwnerModeBeep() {
    playTone(2500, 50);
}

void playGuestModeBeep() {
    playTone(1000, 50); delay(100);
    playTone(1000, 50);
}
