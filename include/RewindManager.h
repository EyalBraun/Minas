#ifndef REWIND_MANAGER_H
#define REWIND_MANAGER_H

#include "CarState.h"
#include "Config.h"
#include <ESP32Servo.h>

/**
 * @class RewindManager
 * @brief Manages standard Rewind and the Shadow Stack (Return to Home).
 */
class RewindManager {
public:
    RewindManager(Servo& steerServo, Servo& throttleESC);

    /**
     * @brief Records current state into both standard history and Shadow Stack.
     */
    void record(int steer, int throttle);

    /**
     * @brief Triggers the 3-second standard rewind.
     */
    void startStandardRewind();

    /**
     * @brief Triggers the full Shadow Stack playback (Return to Home).
     * This is used when connection is lost.
     */
    void returnToHome();

    bool isRewinding() const { return _isRewinding; }

private:
    Servo& _steerServo;
    Servo& _throttleESC;

    // Standard History (Circular)
    CarState _history[MAX_HISTORY_STEPS];
    int _head = 0;
    int _count = 0;

    // Shadow Stack (Circular Buffer in PSRAM)
    CarState* _shadowStack = nullptr;
    int _shadowCount = 0;
    int _shadowHead = 0; // תוספת: ניהול הראש של חוצץ ה-PSRAM

    bool _isRewinding = false;

    CarState* getState(int offset);

};

#endif
