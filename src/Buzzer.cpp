/**
 * @file Buzzer.cpp
 * @project Minas
 * @brief Buzzer implementation using direct LEDC channel control to avoid timer conflicts with ESP32Servo.
 */

#include <Arduino.h>
#include <Buzzer.h>
#include <Config.h>

// נשתמש בערוץ LEDC ייעודי לבאזר (ערוץ 7) כדי לא להתנגש עם הסרוו
#define BUZZER_LEDC_CHANNEL 7

void initBuzzer() {
    ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8); // תדר התחלתי 2kHz, רזולוציה 8 ביט
    ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0); // כבוי בהתחלה
}

void playTone(int frequency, int durationMs) {
    if (frequency > 0) {
        ledcSetup(BUZZER_LEDC_CHANNEL, frequency, 8);
        ledcWrite(BUZZER_LEDC_CHANNEL, 127); // 50% duty cycle
    } else {
        ledcWrite(BUZZER_LEDC_CHANNEL, 0);
    }
    delay(durationMs);
    ledcWrite(BUZZER_LEDC_CHANNEL, 0); // כיבוי
}

void playConnectSound() {
    playTone(523, 80);  delay(90);
    playTone(659, 80);  delay(90);
    playTone(784, 100); delay(100);
    playTone(0, 0);
}

void playDisconnectSound() {
    playTone(784, 100); delay(110);
    playTone(523, 150); delay(150);
    playTone(0, 0);
}

void playRewindSound() {
    playTone(1400, 40); delay(45);
    playTone(1800, 40); delay(45);
    playTone(0, 0);
}

void playMuteWarningSound() {
    // צפצוף קצר וחזק שמתריע שהלוגים מושתקים (Recording Paused)
    playTone(2000, 60);
    playTone(0, 0);
}
