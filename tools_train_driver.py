#!/usr/bin/env python3
"""
===============================================================================
MINAS WROVER DRIVER-RECOGNITION FEATURE EXTRACTION & WINDOWING PIPELINE
===============================================================================

This script prepares Minas ESP32-WROVER trial logs for biometric driver-identification
machine learning models (e.g., Random Forest, XGBoost, LightGBM, SVM, MLP).

Key Pipeline Steps:
1. Parse raw 20 Hz trial CSV files and extract self-describing metadata headers.
2. Segment continuous time-series into overlapping sliding windows (default 40 samples ≈ 2s).
3. Compute summary statistics (mean, std, min, max) across 13 numeric control & sensor metrics.
4. Calculate data quality ratios (sonar validity ratio, controller connectivity ratio).
5. Perform a segment-based, class-stratified train/test split (preventing intra-session leakage).
6. Export the processed feature datasets as tabular CSV files (windows_train.csv & windows_test.csv).
7. Generate a comprehensive JSON audit report (collection_report.json).
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
from statistics import mean, pstdev
from typing import Any, Dict, List, Tuple

# The 13 numerical columns extracted from the 20 Hz vehicle telemetry
NUMERIC_COLUMNS: List[str] = [
    "raw_lx",                 # Left stick X (-128 to 127) - steering input
    "raw_ly",                 # Left stick Y (-128 to 127)
    "raw_rx",                 # Right stick X (-128 to 127)
    "raw_ry",                 # Right stick Y (-128 to 127)
    "l2",                     # Brake / reverse trigger pressure (0 to 255)
    "r2",                     # Throttle trigger pressure (0 to 255)
    "steering_deg",           # Calculated steering angle (0° to 180°)
    "throttle_percent",       # Signed throttle percentage (-100% to +100%)
    "steering_command_deg",   # Constrained steering command sent to servo
    "esc_command_us",         # ESC pulse width command (1000 µs - 2000 µs)
    "steering_delta",         # Rate of change of steering angle
    "throttle_delta",         # Rate of change of throttle percentage
    "sonar_distance_cm",      # Obstacle distance from HC-SR04 sonar
]


def read_trial(path: Path) -> Tuple[Dict[str, str], List[Dict[str, str]]]:
    """
    Parse a single trial CSV file into metadata dictionary and data rows.

    The file format consists of:
    1. Key-value metadata lines (e.g. 'schema_version=1', 'label=owner')
    2. A delimiter line '---'
    3. Standard CSV header and data rows at 20 Hz.
    """
    metadata: Dict[str, str] = {}
    rows: List[Dict[str, str]] = []

    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        header = None
        for fields in reader:
            if not fields:
                continue

            first_field = fields[0].strip()
            if header is None and len(fields) == 1 and "=" in first_field:
                key, value = first_field.split("=", 1)
                metadata[key.strip()] = value.strip()
            elif first_field == "---":
                header_line = next(reader, None)
                if header_line:
                    header = [h.strip() for h in header_line]
            elif header is not None:
                row = dict(zip(header, [f.strip() for f in fields]))
                if row.get("timestamp_ms") and row.get("sample_sequence"):
                    rows.append(row)

    return metadata, rows


def parse_float(row: Dict[str, str], key: str, default: float = 0.0) -> float:
    """Safely parse a numerical string into a finite float, returning default on failure."""
    try:
        val = float(row.get(key, default))
        return val if math.isfinite(val) else default
    except (TypeError, ValueError):
        return default


def make_windows(
    rows: List[Dict[str, str]],
    size: int,
    stride: int,
    path: Path,
    metadata: Dict[str, str],
    start_window_index: int = 0
) -> List[Dict[str, Any]]:
    """
    Construct fixed-length sliding windows from continuous trial samples.

    Each window is converted into a flat feature record with:
    - Window identifiers (segment, timestamps, duration, sample count)
    - Ground-truth identity (label, is_owner)
    - Summary statistics (mean, std, min, max) for each numerical feature
    - Contextual and data-quality ratios
    """
    windows: List[Dict[str, Any]] = []
    window_counter = start_window_index

    label = metadata.get("label")
    if not label:
        label = "owner" if path.name.startswith("owner_segment_") else "nonowner"
    is_owner = 1 if label == "owner" else 0

    for start in range(0, len(rows) - size + 1, stride):
        chunk = rows[start:start + size]
        if not chunk:
            continue

        start_ts = parse_float(chunk[0], "timestamp_ms")
        end_ts = parse_float(chunk[-1], "timestamp_ms")

        record: Dict[str, Any] = {
            "window_id": window_counter,
            "source_segment": path.name,
            "label": label,
            "is_owner": is_owner,
            "start_timestamp_ms": start_ts,
            "end_timestamp_ms": end_ts,
            "duration_ms": max(0.0, end_ts - start_ts),
            "sample_count": len(chunk),
        }
        window_counter += 1

        for column in NUMERIC_COLUMNS:
            default_val = -1.0 if column == "sonar_distance_cm" else 0.0
            values = [parse_float(row, column, default_val) for row in chunk]
            record[f"{column}_mean"] = round(mean(values), 4)
            record[f"{column}_std"] = round(pstdev(values) if len(values) > 1 else 0.0, 4)
            record[f"{column}_min"] = round(min(values), 4)
            record[f"{column}_max"] = round(max(values), 4)

        valid_sonar = [parse_float(row, "sonar_valid") for row in chunk]
        connected = [parse_float(row, "controller_connected", 1.0) for row in chunk]

        record["sonar_valid_ratio"] = round(mean(valid_sonar), 4) if valid_sonar else 0.0
        record["controller_connected_ratio"] = round(mean(connected), 4) if connected else 0.0

        windows.append(record)

    return windows


def write_csv(path: Path, records: List[Dict[str, Any]]) -> None:
    """Write list of feature records into a flat tabular CSV file."""
    if not records:
        return
    fieldnames = list(records[0].keys())
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)


def stratified_split_by_segment(
    windows_by_file: Dict[str, List[Dict[str, Any]]],
    labels_by_file: Dict[str, str],
    test_ratio: float = 0.25
) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]], List[str], List[str]]:
    """
    Perform a class-stratified split by complete segment files.

    Entire segment files are held out together to prevent temporal leakage.
    Segments are grouped by class (owner vs nonowner) and held out
    proportionally, ensuring BOTH classes are represented in train and test.
    """
    train_windows: List[Dict[str, Any]] = []
    test_windows: List[Dict[str, Any]] = []
    held_out_files: List[str] = []
    train_files: List[str] = []

    files_by_label: Dict[str, List[str]] = {}
    for filename, label in labels_by_file.items():
        files_by_label.setdefault(label, []).append(filename)

    for label, files in sorted(files_by_label.items()):
        files = sorted(files)
        num_files = len(files)
        num_test = max(1, int(round(num_files * test_ratio))) if num_files > 1 else 0

        test_subset = set(files[-num_test:]) if num_test > 0 else set()
        for f in files:
            if f in test_subset:
                held_out_files.append(f)
                test_windows.extend(windows_by_file[f])
            else:
                train_files.append(f)
                train_windows.extend(windows_by_file[f])

    return train_windows, test_windows, train_files, held_out_files


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract statistical features from Minas WROVER vehicle logs into CSV windows."
    )
    parser.add_argument(
        "--data-dir", required=True, type=Path,
        help="Path to folder containing exported segment CSV files from the SD card."
    )
    parser.add_argument(
        "--out-dir", required=True, type=Path,
        help="Output directory where windows_train.csv and windows_test.csv will be saved."
    )
    parser.add_argument(
        "--window", type=int, default=40,
        help="Number of samples per window (default: 40 samples ≈ 2.0 seconds at 20 Hz)."
    )
    parser.add_argument(
        "--stride", type=int, default=10,
        help="Sliding window stride in samples (default: 10 samples ≈ 0.5 seconds)."
    )
    parser.add_argument(
        "--test-ratio", type=float, default=0.25,
        help="Fraction of segment files to reserve for the test set (default: 0.25 = 25%)."
    )

    args = parser.parse_args()
    if args.window <= 0 or args.stride <= 0:
        raise SystemExit("Error: --window and --stride must be positive integers.")

    all_files = sorted(args.data_dir.rglob("*.csv"))
    if not all_files:
        raise SystemExit(f"No CSV files found in {args.data_dir}")

    windows_by_file: Dict[str, List[Dict[str, Any]]] = {}
    labels_by_file: Dict[str, str] = {}
    file_reports: List[Dict[str, Any]] = []
    global_window_index = 0

    for path in all_files:
        metadata, rows = read_trial(path)
        if not rows:
            continue

        windows = make_windows(
            rows,
            size=args.window,
            stride=args.stride,
            path=path,
            metadata=metadata,
            start_window_index=global_window_index
        )
        if not windows:
            continue

        global_window_index += len(windows)
        windows_by_file[path.name] = windows
        label = windows[0]["label"]
        labels_by_file[path.name] = label

        file_reports.append({
            "file": path.name,
            "label": label,
            "rows": len(rows),
            "windows": len(windows),
            "metadata": metadata
        })

    if not windows_by_file:
        raise SystemExit("No valid time windows could be created. Ensure files have enough samples.")

    labels_present = set(labels_by_file.values())
    print(f"[INFO] Processed {len(windows_by_file)} segments across classes: {labels_present}")

    train_windows, test_windows, train_files, test_files = stratified_split_by_segment(
        windows_by_file, labels_by_file, test_ratio=args.test_ratio
    )

    args.out_dir.mkdir(parents=True, exist_ok=True)

    train_csv_path = args.out_dir / "windows_train.csv"
    test_csv_path = args.out_dir / "windows_test.csv"
    write_csv(train_csv_path, train_windows)
    write_csv(test_csv_path, test_windows)

    report = {
        "dataset_summary": {
            "total_segments": len(windows_by_file),
            "total_train_windows": len(train_windows),
            "total_test_windows": len(test_windows),
            "labels": sorted(labels_present),
            "window_size_samples": args.window,
            "window_size_seconds": round(args.window * 0.05, 2),
            "stride_samples": args.stride,
            "stride_seconds": round(args.stride * 0.05, 2),
        },
        "segment_split": {
            "training_files": train_files,
            "held_out_test_files": test_files,
        },
        "files_detail": file_reports,
        "notes": [
            "Features are summarized per fixed sliding window.",
            "Complete segment files are held out to prevent temporal autocorrelation leakage.",
            "Use windows_train.csv and windows_test.csv directly in Pandas/Scikit-learn/PyTorch.",
        ],
    }

    report_path = args.out_dir / "collection_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    print("\n[SUCCESS] Feature processing complete:")
    print(f"  - Training windows CSV: {train_csv_path} ({len(train_windows)} rows)")
    print(f"  - Testing windows CSV:  {test_csv_path} ({len(test_windows)} rows)")
    print(f"  - Audit Report:         {report_path}")


if __name__ == "__main__":
    main()
