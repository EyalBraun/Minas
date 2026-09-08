#!/usr/bin/env python3
"""Train and evaluate a Random Forest on Minas feature windows."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import joblib
import pandas as pd
from sklearn.ensemble import RandomForestClassifier
from sklearn.metrics import (accuracy_score, confusion_matrix, f1_score,
                             precision_score, recall_score, roc_auc_score)

META_COLUMNS = {
    "source_segment", "label", "is_owner", "window_id",
    "start_timestamp_ms", "end_timestamp_ms", "duration_ms", "sample_count",
}


def main() -> None:
    parser = argparse.ArgumentParser(description="Train Minas Random Forest")
    parser.add_argument("--train", type=Path, required=True)
    parser.add_argument("--test", type=Path, required=True)
    parser.add_argument("--model-out", type=Path, required=True)
    parser.add_argument("--metrics-out", type=Path, required=True)
    parser.add_argument("--trees", type=int, default=300)
    args = parser.parse_args()

    train = pd.read_csv(args.train)
    test = pd.read_csv(args.test)
    required = {"is_owner", "label"}
    if not required.issubset(train.columns) or not required.issubset(test.columns):
        raise SystemExit("Both CSV files must contain is_owner and label columns")
    if set(train["is_owner"].unique()) != {0, 1}:
        raise SystemExit("Training set must contain both owner and nonowner")
    if set(test["is_owner"].unique()) != {0, 1}:
        raise SystemExit("Test set must contain both owner and nonowner")

    feature_columns = [column for column in train.columns if column not in META_COLUMNS]
    if not feature_columns:
        raise SystemExit("No numeric feature columns found")
    X_train = train[feature_columns].fillna(0.0)
    y_train = train["is_owner"].astype(int)
    X_test = test.reindex(columns=feature_columns).fillna(0.0)
    y_test = test["is_owner"].astype(int)

    model = RandomForestClassifier(
        n_estimators=args.trees,
        random_state=20260908,
        class_weight="balanced",
        n_jobs=-1,
        min_samples_leaf=2,
    )
    model.fit(X_train, y_train)
    probabilities = model.predict_proba(X_test)[:, 1]
    predictions = (probabilities >= 0.5).astype(int)
    matrix = confusion_matrix(y_test, predictions, labels=[0, 1])
    tn, fp, fn, tp = matrix.ravel()
    metrics = {
        "accuracy": accuracy_score(y_test, predictions),
        "precision_owner": precision_score(y_test, predictions, zero_division=0),
        "recall_owner": recall_score(y_test, predictions, zero_division=0),
        "f1_owner": f1_score(y_test, predictions, zero_division=0),
        "roc_auc": roc_auc_score(y_test, probabilities),
        "false_acceptance_rate": float(fp / (fp + tn)) if (fp + tn) else 0.0,
        "false_rejection_rate": float(fn / (fn + tp)) if (fn + tp) else 0.0,
        "confusion_matrix_labels_0_nonowner_1_owner": matrix.tolist(),
        "train_rows": len(train),
        "test_rows": len(test),
        "feature_columns": feature_columns,
        "decision_threshold": 0.5,
    }
    args.model_out.parent.mkdir(parents=True, exist_ok=True)
    args.metrics_out.parent.mkdir(parents=True, exist_ok=True)
    joblib.dump({"model": model, "features": feature_columns}, args.model_out)
    args.metrics_out.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
