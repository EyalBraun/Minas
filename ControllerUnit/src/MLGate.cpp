#include "MLGate.h"
#include "DriverModel.h"
#include <math.h>

namespace {
float clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float sigmoid(float x) {
    if (x >= 18.0f) return 1.0f;
    if (x <= -18.0f) return 0.0f;
    return 1.0f / (1.0f + expf(-x));
}
}

void MovingWindowGate::reset() {
    head_ = 0;
    count_ = 0;
}

void MovingWindowGate::push(float steering, float throttle, float dtSeconds) {
    Sample& sample = samples_[head_];
    sample.steering = clamp01(steering);
    sample.throttle = clamp01(throttle);
    sample.dt = dtSeconds > 0.001f && dtSeconds < 1.0f ? dtSeconds : 0.05f;
    head_ = static_cast<uint16_t>((head_ + 1U) % CAPACITY);
    if (count_ < CAPACITY) ++count_;
}

MLDecision MovingWindowGate::predict() const {
    MLDecision result{0, 0, 0, MINAS_MODEL_READY ? 1 : 0};
    if (!MINAS_MODEL_READY || count_ < MINAS_MODEL_MIN_SAMPLES) return result;
    result.ready = 1;

    float meanSteer = 0.0f;
    float meanThrottle = 0.0f;
    float meanAbsSteerDelta = 0.0f;
    float meanAbsThrottleDelta = 0.0f;
    float meanSteerVariance = 0.0f;
    float meanThrottleVariance = 0.0f;
    float previousSteer = 0.5f;
    float previousThrottle = 0.5f;
    uint16_t start = static_cast<uint16_t>((head_ + CAPACITY - count_) % CAPACITY);

    for (uint16_t i = 0; i < count_; ++i) {
        const Sample& s = samples_[(start + i) % CAPACITY];
        meanSteer += s.steering;
        meanThrottle += s.throttle;
        if (i > 0) {
            meanAbsSteerDelta += fabsf(s.steering - previousSteer);
            meanAbsThrottleDelta += fabsf(s.throttle - previousThrottle);
        }
        previousSteer = s.steering;
        previousThrottle = s.throttle;
    }

    const float n = static_cast<float>(count_);
    meanSteer /= n;
    meanThrottle /= n;
    meanAbsSteerDelta /= count_ > 1 ? static_cast<float>(count_ - 1) : 1.0f;
    meanAbsThrottleDelta /= count_ > 1 ? static_cast<float>(count_ - 1) : 1.0f;

    for (uint16_t i = 0; i < count_; ++i) {
        const Sample& s = samples_[(start + i) % CAPACITY];
        const float ds = s.steering - meanSteer;
        const float dt = s.throttle - meanThrottle;
        meanSteerVariance += ds * ds;
        meanThrottleVariance += dt * dt;
    }
    meanSteerVariance /= n;
    meanThrottleVariance /= n;

    const float features[6] = {
        meanSteer,
        meanThrottle,
        meanAbsSteerDelta,
        meanAbsThrottleDelta,
        meanSteerVariance,
        meanThrottleVariance
    };

    float score = MINAS_MODEL_BIAS;
    for (uint8_t i = 0; i < MINAS_MODEL_FEATURE_COUNT; ++i) {
        const float normalized = (features[i] - MINAS_MODEL_MEAN[i]) /
                                 (MINAS_MODEL_SCALE[i] > 1e-6f ? MINAS_MODEL_SCALE[i] : 1.0f);
        score += normalized * MINAS_MODEL_WEIGHTS[i];
    }

    const float probability = sigmoid(score);
    result.confidence = static_cast<uint16_t>(probability * 1000.0f + 0.5f);
    result.decision = probability >= MINAS_MODEL_THRESHOLD ? 1 : 0;
    return result;
}
