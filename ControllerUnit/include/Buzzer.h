#ifndef BUZZER_H
#define BUZZER_H

void initBuzzer();
void playTone(int frequency, int durationMs);
void playConnectSound();
void playDisconnectSound();
void playObstacleSound();
void playOwnerModeBeep();
void playGuestModeBeep();

#endif
