import os
import glob
import joblib
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.ensemble import RandomForestClassifier
from sklearn.neural_network import MLPClassifier
from sklearn.preprocessing import StandardScaler
from sklearn.model_selection import StratifiedKFold, cross_val_predict
from sklearn.metrics import accuracy_score, confusion_matrix

# קביעת Seed לשחזור התוצאות
np.random.seed(42)

# הגדרות חלון זמן (2.0 שניות חלון, צעד של 0.5 שניות, קצב דגימה 20Hz)
SAMPLING_RATE_HZ = 20
WINDOW_SECONDS = 2.0
STRIDE_SECONDS = 0.5
WINDOW_SIZE = int(WINDOW_SECONDS * SAMPLING_RATE_HZ)
STRIDE_SIZE = int(STRIDE_SECONDS * SAMPLING_RATE_HZ)

# גודל חלון ההחלטה המתגלגל (15 חלונות = 7.5 שניות של החלטה רציפה ויציבה)
ROLLING_WINDOWS = 15


def load_all_forza_data(folder_path="Data"):
    """
    טוען את כל קובצי ה-CSV מתיקיית הנתונים.
    תומך בהפעלה הן מתוך תיקיית poc והן מתוך התיקייה הראשית של הפרויקט.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        folder_path,
        os.path.join(script_dir, folder_path),
        os.path.join(script_dir, "Data"),
        os.path.join(script_dir, "data"),
        os.path.join(os.getcwd(), "poc", "Data"),
        os.path.join(os.getcwd(), "Data"),
        os.path.join(os.getcwd(), "data"),
    ]

    all_files = []
    seen_dirs = set()
    for cand in candidates:
        if os.path.isdir(cand):
            cand_abs = os.path.abspath(cand)
            if cand_abs not in seen_dirs:
                seen_dirs.add(cand_abs)
                found = glob.glob(os.path.join(cand_abs, "*.csv"))
                all_files.extend(found)

    # בדיקה נוספת לקבצים ישירים אם לא נמצאו בתיקיות
    if not all_files:
        all_files.extend(glob.glob("*.csv"))
        all_files.extend(glob.glob(os.path.join(script_dir, "*.csv")))

    # ניקוי כפילויות מוחלט ותאימות Windows (Case-insensitive)
    file_map = {os.path.normcase(os.path.abspath(f)): f for f in all_files}
    all_files = sorted(list(file_map.values()))

    if not all_files:
        raise ValueError(
            f"No CSV files found in '{folder_path}' or '{os.path.join(script_dir, folder_path)}'.\n"
            "Please ensure you placed your recorded files in the poc/Data directory."
        )

    print(f"[*] Found {len(all_files)} CSV data files:")
    for f in all_files:
        print(f"    - {os.path.basename(f)}")

    df_list = []
    for file_idx, file in enumerate(all_files):
        try:
            df = pd.read_csv(file)
            df.columns = [col.strip() for col in df.columns]

            # מיון כרונולוגי לפני חישוב הנגזרות למניעת קפיצות מלאכותיות
            if "timestamp_s" in df.columns:
                df = df.sort_values("timestamp_s", kind="mergesort").reset_index(drop=True)

            # השלמת נגזרות אם חסרות
            if "d_throttle" not in df.columns and "throttle" in df.columns:
                df["d_throttle"] = (df["throttle"].diff() * SAMPLING_RATE_HZ).fillna(0.0)
            if "d_steering" not in df.columns and "steering" in df.columns:
                df["d_steering"] = (df["steering"].diff() * SAMPLING_RATE_HZ).fillna(0.0)
            if "run_number" not in df.columns:
                df["run_number"] = file_idx + 1

            df_list.append(df)
        except Exception as e:
            print(f"[!] Warning: Could not parse {file}: {e}")

    if not df_list:
        raise ValueError("No valid telemetry CSV files could be parsed.")

    full_df = pd.concat(df_list, ignore_index=True)
    return full_df


def extract_window_features(df, return_groups=False):
    """
    חותך את הנסיעה לחלונות דינמיים של 2.0 שניות ומחלץ 16 מאפיינים התנהגותיים
    (Neuromotor Behavioral Biometrics).
    מסנן חלונות עמידה מוחלטת במקום (IDLE) כדי למנוע זיהום של המודל בדגימות אפס.
    """
    features_list = []
    labels_list = []
    groups_list = []

    group_cols = ["driver_name", "track"]
    if "run_number" in df.columns:
        group_cols.append("run_number")

    for keys, group in df.groupby(group_cols):
        if "timestamp_s" in group.columns:
            group = group.sort_values("timestamp_s", kind="mergesort")
        vals = group[["throttle", "brake", "steering", "d_throttle", "d_steering"]].values
        label = int(group["is_authorized"].iloc[0])

        for start_idx in range(0, len(vals) - WINDOW_SIZE + 1, STRIDE_SIZE):
            win = vals[start_idx : start_idx + WINDOW_SIZE]
            th = win[:, 0]
            br = win[:, 1]
            st = win[:, 2]
            d_th = win[:, 3]
            d_st = win[:, 4]

            # דילוג על חלונות שבהם הרכב עומד לחלוטין ללא שום קלט נהיגה (IDLE)
            if (
                np.max(th) < 0.02
                and np.max(br) < 0.02
                and np.max(np.abs(st)) < 0.03
                and np.max(np.abs(d_st)) < 0.05
            ):
                continue

            # חילוץ 16 מאפייני חתימה התנהגותית (שמירה על תאימות מלאה ל-Live PoC)
            feat_vec = [
                np.mean(np.abs(d_st)),                              # 1. קצב ממוצע של תיקוני היגוי (עדינות מול חדות)
                np.std(d_st),                                       # 2. שונות קצב תיקוני היגוי
                np.max(np.abs(d_st)),                               # 3. שיא חדות תיקון ההיגוי בחלון
                np.sum((d_st[:-1] * d_st[1:]) < 0),                 # 4. תדירות היפוכי כיוון אמיתיים בהיגוי
                np.mean(np.abs(st)),                                # 5. משרעת ממוצעת של זווית ההיגוי
                np.std(st),                                         # 6. שונות שימוש בהיגוי
                np.mean(th),                                        # 7. מצערת ממוצעת (אגרסיביות נהיגה)
                np.std(th),                                         # 8. תנודתיות המצערת
                np.mean(np.abs(d_th)),                              # 9. קצב ממוצע של לחיצות/שחרורי גז
                np.std(d_th),                                       # 10. שונות קצב שינוי הגז
                np.mean(br),                                        # 11. יחס שימוש בבלמים
                np.std(br),                                         # 12. שונות בלימה
                float(np.mean(th > 0.85)),                          # 13. שיעור לחיצה על מצערת מלאה
                float(np.mean((th < 0.05) & (br < 0.05))),          # 14. שיעור גלישה ללא דוושות (Coasting ratio)
                float(np.mean(br > 0.10)),                          # 15. תדירות בלימה מובהקת
                float(np.percentile(np.abs(d_st), 90)),             # 16. אחוזון 90 של חדות תיקוני היגוי
            ]
            features_list.append(feat_vec)
            labels_list.append(label)
            groups_list.append(keys)

    X_arr = np.array(features_list, dtype=np.float32)
    y_arr = np.array(labels_list, dtype=np.int32)
    if return_groups:
        return X_arr, y_arr, groups_list
    return X_arr, y_arr


def evaluate_security_metrics(y_true, y_pred):
    """
    מחשב דיוק (Accuracy), שיעור קבלה שגויה של זר (FAR), ושיעור דחייה שגויה של מורשה (FRR).
    """
    tn, fp, fn, tp = confusion_matrix(y_true, y_pred, labels=[0, 1]).ravel()
    acc = accuracy_score(y_true, y_pred) * 100.0
    far = (fp / (fp + tn)) * 100.0 if (fp + tn) > 0 else 0.0
    frr = (fn / (fn + tp)) * 100.0 if (fn + tp) > 0 else 0.0
    return acc, far, frr


def calibrate_optimal_threshold(y_true, probs):
    """
    מכייל את סף ההחלטה (Threshold) באופן שמאזן בין FAR (<5%) לבין FRR (<10%)
    מבלי להקריב את הנהג המורשה.
    """
    best_th = 0.50
    best_score = -float("inf")

    for th in np.arange(0.18, 0.82, 0.01):
        preds = (probs >= th).astype(int)
        acc, far, frr = evaluate_security_metrics(y_true, preds)

        # בונוס עמידה ביעדי המחקר: FAR < 5% וגם FRR < 10%
        bonus = 0.0
        if far <= 5.0:
            bonus += 15.0
        if frr <= 10.0:
            bonus += 15.0
        if far <= 4.5 and frr <= 9.5:
            bonus += 20.0

        # שקלול מאוזן שאינו מעניש את ה-FRR בחסר
        score = acc - (1.4 * far) - (1.2 * frr) + bonus
        if score > best_score:
            best_score = score
            best_th = float(th)

    return best_th


def compute_rolling_probabilities(groups, probs, window=ROLLING_WINDOWS):
    """
    מחשב ממוצע מתגלגל יציב לכל הקלטה בנפרד, תוך מניעת שגיאות Cold-Start
    בחלונות הראשונים של הנסיעה.
    """
    df_roll = pd.DataFrame({
        "group": [str(g) for g in groups],
        "prob": probs,
    })

    def smooth_series(s):
        # שימוש ב-min_periods=3 ומילוי לאחור (bfill) כדי למנוע קפיצות בחלון הראשון
        rolled = s.rolling(window=window, min_periods=3).mean()
        return rolled.bfill().fillna(s.expanding().mean())

    return df_roll.groupby("group", sort=False)["prob"].transform(smooth_series).values


def main():
    print("=" * 84)
    print("=== Minas Project: Real Forza Telemetry Training (Track A,B -> D) ===")
    print("=" * 84)

    # 1. טעינת נתונים
    full_df = load_all_forza_data()

    # זיהוי המסלולים שבהם נהגו כל סוגי הנהגים (גם מורשה וגם לא-מורשה)
    unauth_tracks = set(full_df[full_df["is_authorized"] == 0]["track"].unique())
    auth_tracks = set(full_df[full_df["is_authorized"] == 1]["track"].unique())
    common_tracks = unauth_tracks.intersection(auth_tracks)

    if not common_tracks:
        print("\n[!] ERROR: No track contains both Authorized and Unauthorized drivers!")
        return

    # מסלול המבחן הבלתי-נראה הוא D (אם קיים במסלולים המשותפים)
    if "D" in common_tracks:
        test_track = "D"
        train_tracks = sorted(list(common_tracks - {test_track}))
        if not train_tracks:
            train_tracks = ["D"]
    else:
        test_track = sorted(list(common_tracks))[-1]
        train_tracks = sorted(list(common_tracks - {test_track}))

    print(f"\n[*] Common Training Track(s) shared by all drivers: {train_tracks}")
    print(f"[*] Held-Out Unseen Test Track: {test_track}")

    train_df = full_df[full_df["track"].isin(train_tracks)]
    test_df = full_df[full_df["track"] == test_track]

    # 2. חילוץ חלונות
    X_train, y_train, train_groups = extract_window_features(train_df, return_groups=True)
    X_test, y_test, test_groups = extract_window_features(test_df, return_groups=True)

    print(f"\n[*] Extracted Training Windows: {len(X_train)} (Authorized: {np.sum(y_train==1)}, Unauthorized: {np.sum(y_train==0)})")
    print(f"[*] Extracted Testing Windows:  {len(X_test)} (Authorized: {np.sum(y_test==1)}, Unauthorized: {np.sum(y_test==0)})")

    if len(np.unique(y_train)) < 2:
        print("\n[!] ERROR: Training data contains only ONE class. Need both Authorized and Unauthorized runs.")
        return
    if len(np.unique(y_test)) < 2:
        print("\n[!] ERROR: Test data contains only ONE class.")
        return

    # 3. נרמול מאפיינים (StandardScaler מחושב אך ורק על נתוני האימון!)
    scaler = StandardScaler()
    X_train_scaled = scaler.fit_transform(X_train)
    X_test_scaled = scaler.transform(X_test)

    # 4. אימון המודלים עם רגולריזציה למניעת Overfitting למסלולים A ו-B
    rf_model = RandomForestClassifier(
        n_estimators=200,
        max_depth=5,
        min_samples_leaf=4,
        class_weight="balanced",
        random_state=42,
    )
    rf_model.fit(X_train_scaled, y_train)

    # אימון Tiny Neural Network עם רגולריזציית L2 (alpha=0.15) לשיפור ההכללה במסלול D
    nn_model = MLPClassifier(
        hidden_layer_sizes=(32, 16),
        alpha=0.15,
        max_iter=500,
        random_state=42,
    )
    nn_model.fit(X_train_scaled, y_train)

    # 5. חישוב הסתברויות Out-of-Fold (Cross-Validation) על נתוני האימון לכיול אמין של הסף
    cv = StratifiedKFold(n_splits=5, shuffle=True, random_state=42)
    class_1_rf = list(rf_model.classes_).index(1)
    class_1_nn = list(nn_model.classes_).index(1)

    p_tr_rf = cross_val_predict(rf_model, X_train_scaled, y_train, cv=cv, method="predict_proba")[:, class_1_rf]
    p_te_rf = rf_model.predict_proba(X_test_scaled)[:, class_1_rf]

    p_tr_nn = cross_val_predict(nn_model, X_train_scaled, y_train, cv=cv, method="predict_proba")[:, class_1_nn]
    p_te_nn = nn_model.predict_proba(X_test_scaled)[:, class_1_nn]

    model_prob_pairs = [
        ("Random Forest", p_tr_rf, p_te_rf),
        ("Tiny Neural Net", p_tr_nn, p_te_nn),
    ]

    results = {}
    print("\n" + "=" * 84)
    print(f"=== Blind Evaluation on Unseen Track {test_track} (Out-of-Fold Calibrated on {train_tracks}) ===")
    print("=" * 84)
    print(f"{'Model':<26} | {'Calibrated Th':<13} | {'Accuracy':<10} | {'FAR (<5%)':<10} | {'FRR (<10%)'}")
    print("-" * 84)

    for name, p_tr, p_te in model_prob_pairs:
        best_th = calibrate_optimal_threshold(y_train, p_tr)

        y_pred_d = (p_te >= best_th).astype(int)
        acc_d, far_d, frr_d = evaluate_security_metrics(y_test, y_pred_d)

        results[name] = {"acc": acc_d, "far": far_d, "frr": frr_d, "th": best_th}
        print(f"{name:<26} | {best_th:>13.2f} | {acc_d:>8.1f}% | {far_d:>8.1f}% | {frr_d:>8.1f}%")

    print("-" * 84)

    # 6. הערכה עיוורת ברמת שקלול רכב רציף (Rolling Consensus Buffer)
    print(f"\n=== Vehicle-Level Continuous Decision (Rolling Consensus - Blind on Track {test_track}) ===")
    print("-" * 84)
    print(f"{'Model':<26} | {'Calibrated Th':<13} | {'Accuracy':<10} | {'FAR (<5%)':<10} | {'FRR (<10%)'}")
    print("-" * 84)

    rolling_results = {}
    for name, p_tr, p_te in model_prob_pairs:
        y_roll_tr = compute_rolling_probabilities(train_groups, p_tr, window=ROLLING_WINDOWS)
        best_roll_th = calibrate_optimal_threshold(y_train, y_roll_tr)

        y_roll_test = compute_rolling_probabilities(test_groups, p_te, window=ROLLING_WINDOWS)
        pred_d = (y_roll_test >= best_roll_th).astype(int)
        acc_d, far_d, frr_d = evaluate_security_metrics(y_test, pred_d)

        rolling_results[name] = {"acc": acc_d, "far": far_d, "frr": frr_d, "th": best_roll_th}
        print(f"{name + ' (Roll)':<26} | {best_roll_th:>13.2f} | {acc_d:>8.1f}% | {far_d:>8.1f}% | {frr_d:>8.1f}%")

    print("-" * 84)

    # 7. הפקת הגרף למצגת ולתיעוד הפרויקט
    fig, ax = plt.subplots(figsize=(8.5, 5.5))

    metrics_names = ["Accuracy (%)\n[Target > 90%]", "FAR (%)\n[Target < 5%]", "FRR (%)\n[Target < 10%]"]
    rf_vals = [rolling_results["Random Forest"]["acc"], rolling_results["Random Forest"]["far"], rolling_results["Random Forest"]["frr"]]
    nn_vals = [rolling_results["Tiny Neural Net"]["acc"], rolling_results["Tiny Neural Net"]["far"], rolling_results["Tiny Neural Net"]["frr"]]

    x = np.arange(len(metrics_names))
    width = 0.32
    rects1 = ax.bar(x - width/2, rf_vals, width, label=f"Random Forest (Th={rolling_results['Random Forest']['th']:.2f})", color="#2b5c8f")
    rects2 = ax.bar(x + width/2, nn_vals, width, label=f"Tiny Neural Net (Th={rolling_results['Tiny Neural Net']['th']:.2f})", color="#36a2eb")

    ax.set_ylabel("Percentage (%)", fontsize=11)
    ax.set_title(f"Minas Security Metrics: Track {','.join(train_tracks)} -> Track {test_track}", fontsize=12, fontweight="bold")
    ax.set_xticks(x)
    ax.set_xticklabels(metrics_names, fontsize=10)
    ax.set_ylim(0, 115)
    ax.grid(axis="y", linestyle="--", alpha=0.5)

    # קווי יעד (Targets) של הפרויקט
    ax.axhline(y=90, xmin=0.01, xmax=0.33, color="red", linestyle=":", linewidth=2, label="Accuracy Target (>90%)")
    ax.axhline(y=5, xmin=0.33, xmax=0.66, color="red", linestyle=":", linewidth=2, label="FAR Target (<5%)")
    ax.axhline(y=10, xmin=0.66, xmax=0.99, color="red", linestyle=":", linewidth=2, label="FRR Target (<10%)")
    ax.legend(fontsize=9, loc="upper right")

    for rect in list(rects1) + list(rects2):
        h = rect.get_height()
        ax.annotate(f"{h:.1f}%", xy=(rect.get_x() + rect.get_width()/2, h),
                    xytext=(0, 3), textcoords="offset points", ha="center", va="bottom", fontsize=9, fontweight="bold")

    plt.tight_layout()

    script_dir = os.path.dirname(os.path.abspath(__file__))
    graph_path = os.path.join(script_dir, "forza_real_results_graph.png")
    try:
        plt.savefig(graph_path, dpi=300)
        print(f"[v] Graph saved successfully as '{graph_path}'")
    except Exception as e:
        print(f"[!] Could not save graph to '{graph_path}': {e}")
    finally:
        plt.close(fig)

    auth_drivers = test_df[test_df["is_authorized"] == 1]["driver_name"].unique()

    # 8. ייצוא המודל, המנרמל (Scaler) והסף המכויל עבור מערכת הזיהוי בזמן אמת (Live PoC)
    model_bundle = {
        "nn_model": nn_model,
        "rf_model": rf_model,
        "scaler": scaler,
        "threshold_nn": float(rolling_results["Tiny Neural Net"]["th"]),
        "threshold_rf": float(rolling_results["Random Forest"]["th"]),
        "single_th_nn": float(results["Tiny Neural Net"]["th"]),
        "single_th_rf": float(results["Random Forest"]["th"]),
        "window_size": WINDOW_SIZE,
        "stride_size": STRIDE_SIZE,
        "sampling_rate_hz": SAMPLING_RATE_HZ,
        "authorized_driver": auth_drivers[0] if len(auth_drivers) > 0 else "Eyal",
    }
    model_path = os.path.join(script_dir, "minas_live_model.pkl")
    try:
        joblib.dump(model_bundle, model_path)
        print(f"[v] Live model bundle exported to '{model_path}'")
    except Exception as e:
        print(f"[!] Could not save model to '{model_path}': {e}")


if __name__ == "__main__":
    main()
