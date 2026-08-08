#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>

void initBuzzer();
void playConnectSound();
void playDisconnectSound();
void playRewindSound();
void playMuteWarningSound();

#endif
