#ifndef REWIND_MANAGER_H
#define REWIND_MANAGER_H

#include <Arduino.h>
#include <ESP32Servo.h>
#include "Config.h"

struct CarState {
    int steer;
    int throttle;
    unsigned long time;
};

class RewindManager {
private:
    Servo& _steerServo;
    Servo& _throttleESC;

    CarState _history[MAX_HISTORY_STEPS];
    int _head = 0;
    int _count = 0;

    CarState* _shadowStack = nullptr;
    int _shadowHead = 0;
    int _shadowCount = 0;
    bool _isRewinding = false;

    CarState* getState(int offset);

public:
    RewindManager(Servo& steerServo, Servo& throttleESC);
    void begin(); // Added explicit initialization method
    void record(int steer, int throttle);
    void startStandardRewind();
    void returnToHome();
};

#endif