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

void playRewindSound() {
    playTone(1400, 40); delay(20);
    playTone(1800, 40);
}

void playMuteWarningSound() {
    playTone(2000, 60);
}

// Periodic status sounds
void playOwnerModeBeep() {
    // Single short high beep for Owner
    playTone(2500, 50);
}

void playGuestModeBeep() {
    // Double low beep for Guest/Not-Me
    playTone(1000, 50); delay(100);
    playTone(1000, 50);
}
