#ifndef MINAS_DRIVER_MODEL_H
#define MINAS_DRIVER_MODEL_H

// This file is overwritten by tools/train_model.py after a real dataset is trained.
// Keeping the default disabled prevents an unvalidated model from driving the car.
#define MINAS_MODEL_READY 0
#define MINAS_MODEL_MIN_SAMPLES 40
#define MINAS_MODEL_THRESHOLD 0.70f
#define MINAS_MODEL_FEATURE_COUNT 6
#define MINAS_MODEL_BIAS 0.0f
static const float MINAS_MODEL_MEAN[MINAS_MODEL_FEATURE_COUNT] = {0, 0, 0, 0, 0, 0};
static const float MINAS_MODEL_SCALE[MINAS_MODEL_FEATURE_COUNT] = {1, 1, 1, 1, 1, 1};
static const float MINAS_MODEL_WEIGHTS[MINAS_MODEL_FEATURE_COUNT] = {0, 0, 0, 0, 0, 0};

#endif
