#!/usr/bin/env python3
"""
===============================================================================
MINAS WROVER DRIVER-RECOGNITION FEATURE EXTRACTION & DATASET SPLITTER
===============================================================================

This script prepares Minas ESP32-WROVER 10-minute trial logs (without sonar)
for biometric driver-identification machine learning models.

Key Pipeline Steps:
1. Validates that the input dataset contains the required completed trial files.
2. Performs a class-stratified random split:
   - Holds out 4 balanced complete trial files (2 owner, 2 nonowner) for testing.
   - Assigns the remaining 12 complete trial files (6 owner, 6 nonowner) for training.
3. Copies raw trial files into dedicated 'train_raw' and 'test_raw' directories.
4. Segments continuous 20 Hz time-series into fixed sliding windows (default: 40 samples ≈ 2s).
5. Computes statistical features (mean, std, min, max) across 12 numeric actuator & controller metrics.
6. Exports feature tables as tabular CSV files (train_window/windows.csv and test_window/windows.csv).
7. Generates an experiment audit report (split_report.json).
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import random
import shutil
from pathlib import Path
from statistics import mean, pstdev
from typing import Any, Dict, Iterable, List, Tuple

# The 12 numeric features extracted from the 20 Hz vehicle telemetry (sonar excluded)
NUMERIC_COLUMNS: List[str] = [
    "raw_lx",                 # Left stick horizontal axis (-128 to 127) - steering
    "raw_ly",                 # Left stick vertical axis (-128 to 127)
    "raw_rx",                 # Right stick horizontal axis (-128 to 127)
    "raw_ry",                 # Right stick vertical axis (-128 to 127)
    "l2",                     # Left analog trigger (0 to 255) - brake / reverse
    "r2",                     # Right analog trigger (0 to 255) - throttle
    "steering_deg",           # Normalized steering intent angle (0° to 180°, center 90°)
    "throttle_percent",       # Normalized signed throttle percentage (-100% to +100%)
    "steering_command_deg",   # Constrained angle command sent to steering servo
    "esc_command_us",         # Actuator command pulse width sent to ESC (1000 to 2000 µs)
    "steering_delta",         # Rate of change of steering angle (first derivative)
    "throttle_delta",         # Rate of change of throttle percentage (first derivative)
]


def read_trial(path: Path) -> Tuple[Dict[str, str], List[Dict[str, str]]]:
    """
    Parse a single trial CSV file into a metadata dictionary and telemetry rows.

    The file structure comprises:
    1. Key-value metadata lines (e.g. 'schema_version=3', 'label=owner').
    2. A delimiter line '---'.
    3. Standard CSV header followed by 20 Hz sample rows.
    Also transparently supports standard CSV files without metadata preamble.
    """
    metadata: Dict[str, str] = {}
    rows: List[Dict[str, str]] = []

    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        header: List[str] | None = None
        for fields in reader:
            if not fields:
                continue

            first = fields[0].strip()
            if header is None and len(fields) == 1 and "=" in first:
                key, value = first.split("=", 1)
                metadata[key.strip()] = value.strip()
            elif first == "---":
                header_line = next(reader, None)
                if header_line:
                    header = [item.strip() for item in header_line]
            elif header is None and any(col in fields for col in ["segment_number", "sample_sequence", "raw_lx", "steering_deg", "timestamp_ms"]):
                header = [item.strip() for item in fields]
            elif header is not None and len(fields) == len(header):
                row = dict(zip(header, (item.strip() for item in fields)))
                rows.append(row)

    return metadata, rows


def determine_label(path: Path, metadata: Dict[str, str], rows: List[Dict[str, str]]) -> str:
    """
    Resolve whether a trial belongs to 'owner' or 'nonowner'.

    Checks in priority order:
    1. File metadata block ('label=owner' / 'label=nonowner')
    2. CSV data rows ('label' column)
    3. File name ('nonowner' substring vs 'owner' substring)
    """
    if metadata.get("label") in {"owner", "nonowner"}:
        return metadata["label"]
    if rows and rows[0].get("label") in {"owner", "nonowner"}:
        return rows[0]["label"]
    name_lower = path.name.lower()
    if "nonowner" in name_lower:
        return "nonowner"
    if "owner" in name_lower:
        return "owner"
    return "owner"


def parse_float(row: Dict[str, str], key: str, default: float = 0.0) -> float:
    """Safely convert a CSV value to a finite float, returning default on invalid input."""
    try:
        value = float(row.get(key, default))
        return value if math.isfinite(value) else default
    except (TypeError, ValueError):
        return default


def windows_for_trial(path: Path, size: int, stride: int) -> List[Dict[str, Any]]:
    """
    Construct sliding time-series windows and extract summary features from a single trial.

    For each window of length `size` samples:
    - Extracts window temporal bounds (start, end, duration, sample count).
    - Computes mean, population standard deviation, minimum, and maximum for each numeric column.
    - Computes controller connection ratio.
    """
    metadata, rows = read_trial(path)
    if len(rows) < size:
        return []

    label = determine_label(path, metadata, rows)

    result: List[Dict[str, Any]] = []
    for start in range(0, len(rows) - size + 1, stride):
        chunk = rows[start:start + size]
        start_ts = parse_float(chunk[0], "timestamp_ms", parse_float(chunk[0], "elapsed_ms", 0.0))
        end_ts = parse_float(chunk[-1], "timestamp_ms", parse_float(chunk[-1], "elapsed_ms", 0.0))

        record: Dict[str, Any] = {
            "window_id": f"{path.stem}_w{len(result):04d}",
            "source_segment": path.name,
            "label": label,
            "is_owner": int(label == "owner"),
            "start_timestamp_ms": start_ts,
            "end_timestamp_ms": end_ts,
            "duration_ms": max(0.0, end_ts - start_ts),
            "sample_count": len(chunk),
        }

        for column in NUMERIC_COLUMNS:
            values = [parse_float(row, column) for row in chunk]
            record[f"{column}_mean"] = round(mean(values), 4)
            record[f"{column}_std"] = round(pstdev(values), 4) if len(values) > 1 else 0.0
            record[f"{column}_min"] = round(min(values), 4)
            record[f"{column}_max"] = round(max(values), 4)

        record["controller_connected_ratio"] = round(
            mean(parse_float(row, "controller_connected", 1.0) for row in chunk), 4
        )
        result.append(record)

    return result


def write_csv(path: Path, rows: List[Dict[str, Any]]) -> None:
    """Write list of window feature dictionaries into a tabular CSV file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0].keys()) if rows else ["source_segment", "label", "is_owner"]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def choose_test_files(
    files: List[Path],
    count: int,
    seed: int,
    enforce_count: bool = True
) -> List[Path]:
    """
    Select held-out test segment files using class-stratified balanced random sampling.

    Ensures that both classes ('owner' and 'nonowner') are proportionally represented
    in both the training and testing sets. Whole files are held out to prevent
    temporal data leakage between adjacent sliding windows.
    """
    if enforce_count and len(files) != 16:
        raise ValueError(
            f"Expected exactly 16 complete trial CSV files, but received {len(files)}. "
            "Use --allow-any-count to bypass this check during testing."
        )

    rng = random.Random(seed)
    labels: Dict[Path, str] = {}
    for path in files:
        metadata, rows = read_trial(path)
        labels[path] = determine_label(path, metadata, rows)

    owner_files = sorted([p for p in files if labels[p] == "owner"])
    nonowner_files = sorted([p for p in files if labels[p] == "nonowner"])

    if not owner_files or not nonowner_files:
        raise ValueError(f"Dataset must contain both 'owner' and 'nonowner' files. Found: {set(labels.values())}")

    # Stratified split: allocate half the test slots to owner, half to nonowner
    num_test_owner = max(1, count // 2) if len(owner_files) > 1 else len(owner_files)
    num_test_nonowner = max(1, count - num_test_owner) if len(nonowner_files) > 1 else len(nonowner_files)

    test_owner = rng.sample(owner_files, min(num_test_owner, len(owner_files)))
    test_nonowner = rng.sample(nonowner_files, min(num_test_nonowner, len(nonowner_files)))

    selected = sorted(test_owner + test_nonowner)
    return selected


def copy_files(files: Iterable[Path], destination: Path) -> None:
    """Copy an iterable of files into the destination directory."""
    destination.mkdir(parents=True, exist_ok=True)
    for path in files:
        shutil.copy2(path, destination / path.name)


def process_directory(source: Path, destination: Path, window: int, stride: int) -> List[Dict[str, Any]]:
    """Process all CSV trials in a folder and write the aggregated windows to windows.csv."""
    all_windows: List[Dict[str, Any]] = []
    for path in sorted(source.glob("*.csv")):
        all_windows.extend(windows_for_trial(path, window, stride))
    write_csv(destination / "windows.csv", all_windows)
    return all_windows


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Prepare Minas 16-file 10-minute trial dataset (without sonar) for machine learning."
    )
    parser.add_argument(
        "--data-dir", type=Path, required=True,
        help="Path to folder containing completed 10-minute trial CSV files from SD card."
    )
    parser.add_argument(
        "--out-dir", type=Path, required=True,
        help="Output directory for train_raw, test_raw, train_window, and test_window."
    )
    parser.add_argument(
        "--window", type=int, default=40,
        help="Number of samples per sliding window (default: 40 samples ~ 2.0 seconds at 20 Hz)."
    )
    parser.add_argument(
        "--stride", type=int, default=10,
        help="Sliding window stride in samples (default: 10 samples ~ 0.5 seconds)."
    )
    parser.add_argument(
        "--seed", type=int, default=20260908,
        help="Random seed for reproducible stratified train/test split."
    )
    parser.add_argument(
        "--allow-any-count", action="store_true",
        help="Allow dataset with file count other than 16 (useful during development/testing)."
    )

    args = parser.parse_args()
    if args.window <= 0 or args.stride <= 0:
        parser.error("--window and --stride must be positive integers.")

    files = sorted(args.data_dir.glob("*.csv"))
    if not files:
        raise SystemExit(f"No CSV files found in {args.data_dir}")

    test_files = choose_test_files(
        files, count=4, seed=args.seed, enforce_count=not args.allow_any_count
    )
    test_set = set(test_files)
    train_files = [path for path in files if path not in test_set]

    # Establish target output directory paths
    train_raw = args.out_dir / "train_raw"
    test_raw = args.out_dir / "test_raw"
    train_window = args.out_dir / "train_window"
    test_window = args.out_dir / "test_window"

    for directory in (train_raw, test_raw, train_window, test_window):
        directory.mkdir(parents=True, exist_ok=True)

    # Copy raw trial splits
    copy_files(train_files, train_raw)
    copy_files(test_files, test_raw)

    # Process sliding windows into tabular CSV
    train_windows = process_directory(train_raw, train_window, args.window, args.stride)
    test_windows = process_directory(test_raw, test_window, args.window, args.stride)

    report = {
        "dataset_summary": {
            "total_files": len(files),
            "train_files": [path.name for path in train_files],
            "test_files": [path.name for path in test_files],
            "train_windows": len(train_windows),
            "test_windows": len(test_windows),
            "window_size_samples": args.window,
            "window_size_seconds": round(args.window * 0.05, 2),
            "stride_samples": args.stride,
            "stride_seconds": round(args.stride * 0.05, 2),
            "random_seed": args.seed,
            "features": NUMERIC_COLUMNS,
            "note": "Controller and actuator features only (sonar excluded).",
        }
    }

    report_path = args.out_dir / "split_report.json"
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")

    print("\n[SUCCESS] Minas data preparation complete:")
    print(f"  - Train Raw:    {train_raw} ({len(train_files)} files)")
    print(f"  - Test Raw:     {test_raw} ({len(test_files)} files)")
    print(f"  - Train Window: {train_window / 'windows.csv'} ({len(train_windows)} windows)")
    print(f"  - Test Window:  {test_window / 'windows.csv'} ({len(test_windows)} windows)")
    print(f"  - Split Report: {report_path}")


if __name__ == "__main__":
    main()
