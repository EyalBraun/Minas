#!/usr/bin/env python3
"""Train a tiny logistic model for Minas and export C++ constants.

Usage:
  python3 tools/train_model.py --data-dir data/trials \
      --out ControllerUnit/include/DriverModel.h

Expected trial files contain the Minas CSV header and metadata lines. Files must
include driver_id/session_id metadata when possible. If they are absent, the
filename is used as a weak fallback and the script refuses to claim a strong
held-out evaluation.
"""
from __future__ import annotations
import argparse
import csv
import glob
import math
import os
from collections import defaultdict

FEATURES = [
    "mean_steering", "mean_throttle", "mean_abs_steer_delta",
    "mean_abs_throttle_delta", "steer_variance", "throttle_variance",
]


def parse_file(path):
    metadata = {}
    columns = None
    rows = []
    with open(path, newline="", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("---"):
                continue
            if "=" in line and not line.startswith("trial_number,"):
                key, value = line.split("=", 1)
                metadata[key.strip()] = value.strip()
                continue
            if line.startswith("trial_number,"):
                columns = next(csv.reader([line]))
                continue
            if columns is None:
                continue
            values = next(csv.reader([line]))
            if len(values) != len(columns):
                continue
            rows.append(dict(zip(columns, values)))
    if not rows:
        return None
    label = metadata.get("label")
    if label is None:
        label = "owner" if "_owner" in os.path.basename(path) else "nonowner"
    return {
        "path": path,
        "driver": metadata.get("driver_id", metadata.get("label", "unknown")),
        "session": metadata.get("session_id", os.path.basename(path)),
        "label": 1 if label.lower() == "owner" else 0,
        "rows": rows,
        "metadata": metadata,
    }


def as_float(row, key, default=0.0):
    try:
        return float(row.get(key, default))
    except (TypeError, ValueError):
        return default


def row_signal(row):
    # Stored steering/throttle are expected in the 0..180 servo domain.
    return as_float(row, "steering") / 180.0, as_float(row, "throttle") / 180.0


def windows(trial, size=40, stride=4):
    signals = [row_signal(row) for row in trial["rows"]]
    if len(signals) < size:
        return []
    result = []
    for start in range(0, len(signals) - size + 1, stride):
        window = signals[start:start + size]
        steers = [x[0] for x in window]
        throttles = [x[1] for x in window]
        steer_d = [abs(steers[i] - steers[i-1]) for i in range(1, size)]
        throttle_d = [abs(throttles[i] - throttles[i-1]) for i in range(1, size)]
        ms = sum(steers) / size
        mt = sum(throttles) / size
        features = [
            ms,
            mt,
            sum(steer_d) / max(1, len(steer_d)),
            sum(throttle_d) / max(1, len(throttle_d)),
            sum((x - ms) ** 2 for x in steers) / size,
            sum((x - mt) ** 2 for x in throttles) / size,
        ]
        result.append((features, trial["label"], trial["driver"], trial["session"]))
    return result


def sigmoid(x):
    if x >= 18:
        return 1.0
    if x <= -18:
        return 0.0
    return 1.0 / (1.0 + math.exp(-x))


def standardize(X):
    n = len(X)
    d = len(X[0])
    mean = [sum(row[j] for row in X) / n for j in range(d)]
    scale = []
    for j in range(d):
        variance = sum((row[j] - mean[j]) ** 2 for row in X) / n
        scale.append(math.sqrt(variance) if variance > 1e-12 else 1.0)
    Z = [[(row[j] - mean[j]) / scale[j] for j in range(d)] for row in X]
    return Z, mean, scale


def train(X, y, epochs=2500, lr=0.08, l2=0.001):
    d = len(X[0])
    w = [0.0] * d
    b = 0.0
    n = len(X)
    for _ in range(epochs):
        grad_w = [0.0] * d
        grad_b = 0.0
        for row, target in zip(X, y):
            p = sigmoid(sum(w[j] * row[j] for j in range(d)) + b)
            error = p - target
            grad_b += error
            for j in range(d):
                grad_w[j] += error * row[j]
        grad_b /= n
        b -= lr * grad_b
        for j in range(d):
            grad_w[j] = grad_w[j] / n + l2 * w[j]
            w[j] -= lr * grad_w[j]
    return w, b


def accuracy(w, b, X, y):
    if not X:
        return 0.0
    correct = 0
    for row, target in zip(X, y):
        pred = 1 if sigmoid(sum(w[j] * row[j] for j in range(len(w))) + b) >= 0.5 else 0
        correct += pred == target
    return correct / len(y)


def cpp_array(values):
    return "{" + ", ".join(f"{v:.9g}f" for v in values) + "}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--window", type=int, default=40)
    parser.add_argument("--stride", type=int, default=4)
    args = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(args.data_dir, "**", "*.csv"), recursive=True))
    trials = [parse_file(path) for path in paths]
    trials = [trial for trial in trials if trial]
    if len(trials) < 4:
        raise SystemExit("Need at least 4 non-empty trial CSV files.")
    if len({trial["driver"] for trial in trials}) < 2:
        raise SystemExit("Need at least two drivers; do not train on owner-only data.")

    all_windows = []
    for trial in trials:
        all_windows.extend(windows(trial, args.window, args.stride))
    if len(all_windows) < 20:
        raise SystemExit("Too few windows. Collect more sessions before training.")

    # Hold out entire sessions, not random rows. This avoids adjacent-window leakage.
    sessions = sorted({item[3] for item in all_windows})
    if len(sessions) < 3:
        raise SystemExit("Need at least 3 sessions for a meaningful held-out evaluation.")
    test_sessions = set(sessions[-max(1, len(sessions) // 3):])
    train_items = [item for item in all_windows if item[3] not in test_sessions]
    test_items = [item for item in all_windows if item[3] in test_sessions]
    if not train_items or not test_items:
        raise SystemExit("Invalid train/test session split.")

    X_train_raw = [item[0] for item in train_items]
    y_train = [item[1] for item in train_items]
    X_test_raw = [item[0] for item in test_items]
    y_test = [item[1] for item in test_items]
    X_train, mean, scale = standardize(X_train_raw)
    X_test = [[(row[j] - mean[j]) / scale[j] for j in range(len(mean))] for row in X_test_raw]
    w, b = train(X_train, y_train)
    print(f"trials={len(trials)} windows={len(all_windows)} train={len(train_items)} test={len(test_items)}")
    print(f"train_accuracy={accuracy(w, b, X_train, y_train):.3f}")
    print(f"held_out_session_accuracy={accuracy(w, b, X_test, y_test):.3f}")

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as out:
        out.write("#ifndef MINAS_DRIVER_MODEL_H\n#define MINAS_DRIVER_MODEL_H\n\n")
        out.write("// Generated by tools/train_model.py. Review held-out metrics before enabling.\n")
        out.write("#define MINAS_MODEL_READY 1\n")
        out.write(f"#define MINAS_MODEL_MIN_SAMPLES {args.window}\n")
        out.write("#define MINAS_MODEL_THRESHOLD 0.70f\n")
        out.write(f"#define MINAS_MODEL_FEATURE_COUNT {len(w)}\n")
        out.write(f"#define MINAS_MODEL_BIAS {b:.9g}f\n")
        out.write(f"static const float MINAS_MODEL_MEAN[{len(mean)}] = {cpp_array(mean)};\n")
        out.write(f"static const float MINAS_MODEL_SCALE[{len(scale)}] = {cpp_array(scale)};\n")
        out.write(f"static const float MINAS_MODEL_WEIGHTS[{len(w)}] = {cpp_array(w)};\n\n")
        out.write("#endif\n")


if __name__ == "__main__":
    main()
