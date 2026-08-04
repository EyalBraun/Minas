#include "RewindManager.h"

RewindManager::RewindManager(Servo& steerServo, Servo& throttleESC) 
    : _steerServo(steerServo), _throttleESC(throttleESC) {
    
    // הקצאת זיכרון מפורשת מ-PSRAM עם בדיקת תקינות
    _shadowStack = (CarState*)ps_malloc(MAX_SHADOW_STEPS * sizeof(CarState));
    if (_shadowStack == nullptr) {
        Serial.println("[CRITICAL ERROR] PSRAM Allocation Failed for Shadow Stack!");
    }
}

void RewindManager::record(int steer, int throttle) {
    if (_isRewinding) return;

    unsigned long now = millis();

    // 1. Record to Standard History
    _history[_head] = {steer, throttle, now};
    _head = (_head + 1) % MAX_HISTORY_STEPS;
    if (_count < MAX_HISTORY_STEPS) _count++;

    // 2. Record to Shadow Stack (Circular Buffer)
    if (_shadowStack != nullptr) {
        _shadowStack[_shadowHead] = {steer, throttle, now};
        _shadowHead = (_shadowHead + 1) % MAX_SHADOW_STEPS;
        if (_shadowCount < MAX_SHADOW_STEPS) _shadowCount++;
    }
}

void RewindManager::startStandardRewind() {
    if (_count < 2 || _isRewinding) return;
    _isRewinding = true;

    Serial.println("[Minas] Standard Rewind Initiated...");

    // Double-Pump קצר ל-ESC כדי לשחרר נעילת בילום ברוורס
    _throttleESC.write(90); delay(50);
    _throttleESC.write(60); delay(50);
    _throttleESC.write(90); delay(50);

    for (int i = 0; i < _count - 1; i++) {
        CarState* current = getState(i);
        CarState* prev = getState(i + 1);

        // היפוך מצערת: 90 הוא ניוטרל, 180-throttle הופך קדימה לרוורס
        int invertedThrottle = 180 - current->throttle;

        _steerServo.write(current->steer);
        _throttleESC.write(invertedThrottle);

        unsigned long delta = current->time - prev->time;
        if (delta > 500) delta = RECORD_INTERVAL_MS;
        delay(delta);
    }

    _count = 0;
    _isRewinding = false;
}

void RewindManager::returnToHome() {
    if (_shadowCount < 2 || _isRewinding || _shadowStack == nullptr) return;
    _isRewinding = true;

    Serial.println("[FAIL-SAFE] Shadow Stack Triggered: Returning to Home...");

    // Double-Pump קצר ל-ESC
    _throttleESC.write(90); delay(50);
    _throttleESC.write(60); delay(50);
    _throttleESC.write(90); delay(50);

    // קריאה אחורה מתוך חוצץ מעגלי
    for (int i = 0; i < _shadowCount - 1; i++) {
        int currIdx = (_shadowHead - 1 - i + MAX_SHADOW_STEPS) % MAX_SHADOW_STEPS;
        int prevIdx = (_shadowHead - 2 - i + MAX_SHADOW_STEPS) % MAX_SHADOW_STEPS;

        CarState* current = &_shadowStack[currIdx];
        CarState* prev = &_shadowStack[prevIdx];

        int invertedThrottle = 180 - current->throttle;

        _steerServo.write(current->steer);
        _throttleESC.write(invertedThrottle);

        unsigned long delta = current->time - prev->time;
        if (delta > 500) delta = RECORD_INTERVAL_MS;
        
        delay(delta / 2); // חזרה במהירות כפולה
    }

    _shadowCount = 0;
    _shadowHead = 0;
    _isRewinding = false;
}

CarState* RewindManager::getState(int offset) {
    int index = (_head - 1 - offset + MAX_HISTORY_STEPS) % MAX_HISTORY_STEPS;
    return &_history[index];
}