#ifndef MINAS_ML_GATE_H
#define MINAS_ML_GATE_H

#include <stdint.h>

struct MLDecision {
    uint8_t decision;       // 1 = owner/continue, 0 = non-owner/stop
    uint16_t confidence;    // 0..1000
    uint8_t ready;          // enough samples for a decision
    uint8_t valid;          // generated model is available
};

class MovingWindowGate {
public:
    static constexpr uint16_t CAPACITY = 40; // 2 seconds at 20 Hz
    void reset();
    void push(float steering, float throttle, float dtSeconds);
    MLDecision predict() const;
    uint16_t size() const { return count_; }

private:
    struct Sample {
        float steering;
        float throttle;
        float dt;
    };

    Sample samples_[CAPACITY] = {};
    uint16_t head_ = 0;
    uint16_t count_ = 0;
};

#endif
