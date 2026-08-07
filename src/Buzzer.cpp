#include <Arduino.h>
#include "Config.h"
#include "Buzzer.h"

// Use LEDC Channel 7 for the buzzer to avoid clashing with ESP32Servo (which uses channels 0-3)
#define BUZZER_CHANNEL 7 

void initBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    // Attach buzzer pin to LEDC channel 7 with a 2000Hz base frequency
    ledcSetup(BUZZER_CHANNEL, 2000, 8); 
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
}

void playTone(int frequency, int duration) {
    if (frequency == 0) {
        ledcWrite(BUZZER_CHANNEL, 0); // Turn off sound (Duty cycle 0)
    } else {
        ledcSetup(BUZZER_CHANNEL, frequency, 8);
        ledcWrite(BUZZER_CHANNEL, 127); // 50% duty cycle
    }
    delay(duration);
    ledcWrite(BUZZER_CHANNEL, 0); // Stop sound after duration
}

void playConnectSound() {
    initBuzzer();
    playTone(1000, 100);
    delay(50);
    playTone(1500, 150);
}

void playDisconnectSound() {
    initBuzzer();
    playTone(1200, 150);
    delay(50);
    playTone(600, 300);
}

void playRewindSound() {
    initBuzzer();
    playTone(800, 80);
    delay(30);
    playTone(1200, 80);
}