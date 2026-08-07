#ifndef BUZZER_H
#define BUZZER_H

#include <Arduino.h>
void initBuzzer();
void playConnectSound(); //Plays an ascending tone sequence to indicate a successful Bluetooth connection.
void playDisconnectSound(); //Plays a descending tone sequence to warn about connection loss.
void playRewindSound();//Plays a quick double-beep to signal the start of a manual rewind action.

#endif
