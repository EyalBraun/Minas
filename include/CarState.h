#ifndef CAR_STATE_H
#define CAR_STATE_H

#include <Arduino.h>

/**
 * @brief Represents the state of the car at a specific point in time.
 */
struct CarState {
    int steer;          // Steering angle (0-180)
    int throttle;       // Throttle value (0-180, 90 is neutral)
    unsigned long time; // Timestamp in milliseconds
};

#endif
