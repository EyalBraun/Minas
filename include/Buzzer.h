#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

void initBuzzer();
void playConnectSound();
void playDisconnectSound();
void playObstacleSound();
void playMuteWarningSound();

// Status sounds
void playOwnerModeBeep();
void playGuestModeBeep();

#endif
