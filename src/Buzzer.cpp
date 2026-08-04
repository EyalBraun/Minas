#include <Buzzer.h>
#include <Config.h>

void playConnectSound() {
    tone(BUZZER_PIN, 523, 80); delay(90);
    tone(BUZZER_PIN, 659, 80); delay(90);
    tone(BUZZER_PIN, 784, 100); delay(100);
    noTone(BUZZER_PIN);
}

void playDisconnectSound() {
    tone(BUZZER_PIN, 784, 100); delay(110);
    tone(BUZZER_PIN, 523, 150); delay(150);
    noTone(BUZZER_PIN);
}

void playRewindSound() {
    // צפצוף מהיר ללא השהיות מיותרות במערכת
    tone(BUZZER_PIN, 1400, 40); delay(45);
    tone(BUZZER_PIN, 1800, 40); delay(45);
    noTone(BUZZER_PIN);
}