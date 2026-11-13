import os
import sys
import time
import socket
import struct
import threading
import argparse
from collections import deque
import joblib
import numpy as np
import pandas as pd

try:
    import winsound
    HAS_WINSOUND = True
except ImportError:
    HAS_WINSOUND = False

try:
    from colorama import init as colorama_init, Fore, Back, Style
    colorama_init(autoreset=True)
except ImportError:
    class _Dummy:
        def __getattr__(self, _):
            return ""
    Fore = Back = Style = _Dummy()

# קבועי דגימה וחלון זמן (זהים ל-train_biometric_model.py ול-ESP32)
SAMPLING_RATE_HZ = 20
DT = 1.0 / SAMPLING_RATE_HZ          # 0.05s (50ms)
WINDOW_SECONDS = 2.0
STRIDE_SECONDS = 0.5
WINDOW_SIZE = int(WINDOW_SECONDS * SAMPLING_RATE_HZ)   # 40 דגימות (2.0 שניות)
STRIDE_SIZE = int(STRIDE_SECONDS * SAMPLING_RATE_HZ)   # 10 דגימות (0.5 שניות)
ALARM_CONSECUTIVE_WINDOWS = 8                          # 8 חלונות סמוכים רצופים (4.0 שניות) לאישור נהג זר


def extract_single_window_features(win_array: np.ndarray) -> np.ndarray:
    """
    מקבל מטריצה בגודל (40, 5) של [throttle, brake, steering, d_throttle, d_steering]
    ומחלץ את 16 המאפיינים ההתנהגותיים הביומטריים (בדיוק כמו ב-train_model.py).
    """
    th = win_array[:, 0]
    br = win_array[:, 1]
    st = win_array[:, 2]
    d_th = win_array[:, 3]
    d_st = win_array[:, 4]

    feat_vec = [
        np.mean(np.abs(d_st)),                              # 1. קצב ממוצע של תיקוני היגוי
        np.std(d_st),                                       # 2. שונות קצב תיקוני היגוי
        np.max(np.abs(d_st)),                               # 3. שיא חדות תיקון ההיגוי בחלון
        np.sum((d_st[:-1] * d_st[1:]) < 0),                 # 4. תדירות היפוכי כיוון בהיגוי
        np.mean(np.abs(st)),                                # 5. משרעת ממוצעת של זווית ההיגוי
        np.std(st),                                         # 6. שונות שימוש בהיגוי
        np.mean(th),                                        # 7. מצערת ממוצעת
        np.std(th),                                         # 8. תנודתיות המצערת
        np.mean(np.abs(d_th)),                              # 9. קצב ממוצע של לחיצות/שחרורי גז
        np.std(d_th),                                       # 10. שונות קצב שינוי הגז
        np.mean(br),                                        # 11. יחס שימוש בבלמים
        np.std(br),                                         # 12. שונות בלימה
        float(np.mean(th > 0.85)),                          # 13. שיעור לחיצה על מצערת מלאה
        float(np.mean((th < 0.05) & (br < 0.05))),          # 14. שיעור גלישה ללא דוושות
        float(np.mean(br > 0.10)),                          # 15. תדירות בלימה מובהקת
        float(np.percentile(np.abs(d_st), 90)),             # 16. אחוזון 90 של חדות תיקוני היגוי
    ]
    return np.array([feat_vec], dtype=np.float32)


def load_or_train_model():
    """
    טוען את המודל המאומן וה-Scaler מתוך minas_live_model.pkl.
    אם הקובץ עדיין לא קיים, מריץ אוטומטית את train_biometric_model.py כדי לייצר אותו.
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(script_dir, "minas_live_model.pkl"),
        os.path.join("poc", "minas_live_model.pkl"),
        "minas_live_model.pkl",
    ]
    for path in candidates:
        if os.path.isfile(path):
            print(f"[*] Loading trained model bundle from: {path}")
            return joblib.load(path)

    print("[!] Model bundle 'minas_live_model.pkl' not found. Training now via train_model.py...")
    if script_dir not in sys.path:
        sys.path.insert(0, script_dir)
    import train_model
    train_model.main()

    for path in candidates:
        if os.path.isfile(path):
            return joblib.load(path)

    raise FileNotFoundError("Could not find or generate 'minas_live_model.pkl'.")


def parse_forza_udp_packet(data: bytes):
    """
    מפענח חבילת UDP של Forza Data Out לפי גודל החבילה המדויק:
    1. ב-Forza Horizon 4/5 (חבילת Dash של 324 בייטים, הכוללת 12 בייטים של Horizon Car Info):
       - מהירות (Speed m/s):     בייט 256 (F32)
       - מצערת (Throttle/Accel): בייט 315 (U8: 0..255)
       - בלימה (Brake):          בייט 316 (U8: 0..255)
       - היגוי (Steering):        בייט 320 (S8: -127..127)
    2. ב-Forza Motorsport 7 / FM (חבילת Dash של 311 בייטים):
       - מהירות (Speed m/s):     בייט 244 (F32)
       - מצערת (Throttle/Accel): בייט 303 (U8)
       - בלימה (Brake):          בייט 304 (U8)
       - היגוי (Steering):        בייט 308 (S8)
    """
    n = len(data)
    if n < 311:
        return False, 0.0, 0.0, 0.0, 0.0

    is_race_on = struct.unpack_from("<i", data, 0)[0] == 1

    if n >= 324:
        # Forza Horizon (324 Bytes) - התאמה של 100% לבייטים של forza_telemetry_logger.py
        raw_accel = data[315]
        raw_brake = data[316]
        raw_steer = struct.unpack_from("<b", data, 320)[0]
        speed_mps = abs(struct.unpack_from("<f", data, 256)[0])
    else:
        # Forza Motorsport Dash (311 Bytes)
        raw_accel = data[303]
        raw_brake = data[304]
        raw_steer = struct.unpack_from("<b", data, 308)[0]
        speed_mps = abs(struct.unpack_from("<f", data, 244)[0])

    if not np.isfinite(speed_mps):
        speed_mps = 0.0

    throttle = round(float(np.clip(raw_accel / 255.0, 0.0, 1.0)), 4)
    brake = round(float(np.clip(raw_brake / 255.0, 0.0, 1.0)), 4)
    steering = round(float(np.clip(raw_steer / 127.0, -1.0, 1.0)), 4)

    # כאשר המשחק בתפריט עצירה (Pause) ואין קלט דוושות/הגה, מאפסים מהירות כדי לעבור ל-IDLE מיד
    if not is_race_on and throttle == 0.0 and brake == 0.0 and steering == 0.0:
        speed_mps = 0.0

    return is_race_on, throttle, brake, steering, float(speed_mps)


def trigger_alarm_beep(async_beep=True):
    """
    משמיע צפצוף התרעה עדין ב-Windows (רק אחת ל-8 חלונות סמוכים, כדי לא להכאיב באוזניים).
    """
    if not HAS_WINSOUND:
        return

    def _beep():
        try:
            winsound.Beep(950, 200)
        except Exception:
            pass

    if async_beep:
        threading.Thread(target=_beep, daemon=True).start()
    else:
        _beep()


class LiveHUDWindow:
    """
    חלון צף (Always-On-Top Overlay) להצגה מעל מסך המשחק של Forza.
    מציג אך ורק האם הנהג מורשה (ירוק), עומד במקום (IDLE), או לא מורשה (אדום מהבהב),
    והאם הזמזם (Buzzer) פועל או כבוי.
    """
    def __init__(self, auth_driver_name="Eyal", threshold=0.45):
        self.enabled = False
        self.flash_toggle = False
        try:
            import tkinter as tk
            self.tk = tk
            self.root = tk.Tk()
            self.root.title("Minas Live Biometric Guard (PoC)")
            self.root.geometry("440x175+30+30")
            self.root.attributes("-topmost", True)
            self.root.configure(bg="#2b2d42")
            self.root.protocol("WM_DELETE_WINDOW", self._on_close)

            self.title_lbl = tk.Label(
                self.root,
                text="MINAS LIVE BIOMETRIC GUARD  [IDLE]",
                font=("Segoe UI", 11, "bold"),
                fg="#d8f3dc",
                bg="#2b2d42",
            )
            self.title_lbl.pack(pady=(14, 6))

            self.status_lbl = tk.Label(
                self.root,
                text="⏸ IDLE",
                font=("Segoe UI", 24, "bold"),
                fg="white",
                bg="#2b2d42",
            )
            self.status_lbl.pack(pady=6)

            self.buzzer_lbl = tk.Label(
                self.root,
                text="🔇 BUZZER: OFF",
                font=("Segoe UI", 15, "bold"),
                fg="#adb5bd",
                bg="#2b2d42",
            )
            self.buzzer_lbl.pack(pady=6)

            self.enabled = True
        except Exception as e:
            print(f"[!] GUI Overlay disabled ({e}). Running in Terminal HUD mode.")
            self.enabled = False

    def _on_close(self):
        self.enabled = False
        try:
            self.root.destroy()
        except Exception:
            pass

    def update_view(self, state):
        if not self.enabled:
            return
        try:
            if state == "IDLE":
                bg_color = "#2b2d42"
                top_text = "MINAS LIVE BIOMETRIC GUARD  [IDLE]"
                status_text = "⏸ IDLE"
                buzzer_text = "🔇 BUZZER: OFF"
                buzzer_fg = "#adb5bd"
            elif state == "ALARM":
                self.flash_toggle = not self.flash_toggle
                bg_color = "#d90429" if self.flash_toggle else "#6a040f"
                top_text = "MINAS LIVE BIOMETRIC GUARD"
                status_text = "🚨 UNAUTHORIZED 🚨"
                buzzer_text = "🔊 BUZZER: ON (BUZZING)"
                buzzer_fg = "#ffffff"
            else:
                bg_color = "#1b4332"
                top_text = "MINAS LIVE BIOMETRIC GUARD"
                status_text = "✔ AUTHORIZED"
                buzzer_text = "🔇 BUZZER: OFF"
                buzzer_fg = "#b7e4c7"

            self.root.configure(bg=bg_color)
            self.title_lbl.configure(bg=bg_color, text=top_text)
            self.status_lbl.configure(bg=bg_color, text=status_text)
            self.buzzer_lbl.configure(bg=bg_color, text=buzzer_text, fg=buzzer_fg)

            self.root.update_idletasks()
            self.root.update()
        except Exception:
            self.enabled = False


class MinasLiveEngine:
    """
    מנוע הזיהוי בזמן אמת (Circular Buffer של 40 דגימות + מונה התרעות רציף של 8 חלונות סמוכים).
    """
    def __init__(self, bundle, use_gui=True, custom_threshold=None, model_type="nn"):
        self.model_type = model_type.lower()
        self.nn_model = bundle.get("nn_model")
        self.rf_model = bundle.get("rf_model")
        self.active_model = self.nn_model if self.model_type == "nn" else self.rf_model
        self.scaler = bundle["scaler"]
        self.auth_driver = bundle.get("authorized_driver", "Eyal")

        # סף החלטה מכויל עבור המודל הנבחר
        if self.model_type == "nn":
            default_th = float(bundle.get("threshold_nn", 0.45))
        else:
            default_th = float(bundle.get("threshold_rf", 0.46))
        self.threshold = custom_threshold if custom_threshold is not None else default_th

        self.buffer = deque(maxlen=WINDOW_SIZE)
        self.prob_history = deque(maxlen=4)
        self.samples_since_eval = 0
        self.unauth_counter = 0

        self.prev_th = 0.0
        self.prev_st = 0.0
        self.has_prev = False

        self.hud = LiveHUDWindow(auth_driver_name=self.auth_driver, threshold=self.threshold) if use_gui else None

    def push_sample(self, throttle: float, brake: float, steering: float, d_th=None, d_st=None, sim_tag="", speed_mps=None):
        """
        מקבל דגימה חדשה בקצב 20Hz, מעדכן את ה-Circular Buffer,
        ומבצע שלב סיווג בכל חצי שנייה (10 דגימות).
        """
        if not self.has_prev:
            d_throttle = 0.0 if d_th is None else d_th
            d_steering = 0.0 if d_st is None else d_st
            self.has_prev = True
        else:
            d_throttle = (throttle - self.prev_th) / DT if d_th is None else d_th
            d_steering = (steering - self.prev_st) / DT if d_st is None else d_st

        self.prev_th = throttle
        self.prev_st = steering

        # בדיקה מידית אם הרכב עומד במקום במשחק (מהירות כמעט 0 וללא גז/היגוי)
        car_stopped_in_game = (
            (speed_mps is not None)
            and (speed_mps < 0.8)
            and (throttle < 0.02)
            and (abs(steering) < 0.03)
        )
        if car_stopped_in_game:
            self.unauth_counter = 0
            self.prob_history.clear()
            self.has_prev = False
            if self.hud:
                self.hud.update_view("IDLE")
            self.samples_since_eval += 1
            if self.samples_since_eval >= STRIDE_SIZE:
                self.samples_since_eval = 0
                self.print_terminal_status("IDLE")
            return "IDLE", 0.5, 0

        # הוספה רציפה ל-Circular Buffer (חלון של 40 דגימות = 2.0 שניות)
        self.buffer.append([throttle, brake, steering, d_throttle, d_steering])
        self.samples_since_eval += 1

        if len(self.buffer) < WINDOW_SIZE:
            if self.hud:
                self.hud.update_view("AUTHORIZED")
            return None

        # הרצת המודל בכל STRIDE_SIZE דגימות (כל 0.5 שניות = 10 דגימות)
        if self.samples_since_eval >= STRIDE_SIZE:
            self.samples_since_eval = 0
            win_arr = np.array(self.buffer, dtype=np.float32)

            # בדיקה אם אין שום פקודת נהיגה לאורך חלון ה-2.0 שניות (עמידה במקום / IDLE)
            is_idle_controls = (
                (np.max(win_arr[:, 0]) < 0.02)
                and (np.max(win_arr[:, 1]) < 0.02)
                and (np.max(np.abs(win_arr[:, 2])) < 0.03)
                and (np.max(np.abs(win_arr[:, 4])) < 0.05)
            )
            if is_idle_controls:
                self.unauth_counter = 0
                self.prob_history.clear()
                self.has_prev = False
                state = "IDLE"
                self.print_terminal_status(state)
                if self.hud:
                    self.hud.update_view(state)
                return state, 0.5, 0

            feats = extract_single_window_features(win_arr)
            feats_scaled = self.scaler.transform(feats)

            # חישוב הסתברות ישירה מהמודל הנבחר (Tiny Neural Net כברירת מחדל או Random Forest)
            class_idx = list(self.active_model.classes_).index(1)
            raw_prob = float(self.active_model.predict_proba(feats_scaled)[0, class_idx])

            self.prob_history.append(raw_prob)
            smooth_prob = float(np.mean(self.prob_history))

            # לוגיקת מונה ההתרעות: מצפצף אך ורק כאשר יש 8 חלונות סמוכים רצופים שמאשרים נהג זר
            unlock_th = self.threshold + 0.12 if self.unauth_counter >= ALARM_CONSECUTIVE_WINDOWS else self.threshold
            if smooth_prob >= unlock_th:
                self.unauth_counter = 0
                state = "AUTHORIZED"
            else:
                self.unauth_counter += 1
                if self.unauth_counter >= ALARM_CONSECUTIVE_WINDOWS:
                    state = "ALARM"
                else:
                    state = "AUTHORIZED"

            self.print_terminal_status(state)
            if self.hud:
                self.hud.update_view(state)

            # צפצוף רק אחת ל-8 חלונות סמוכים של נהג לא מורשה (ולא בכל חלון בודד!)
            if state == "ALARM" and (self.unauth_counter % ALARM_CONSECUTIVE_WINDOWS == 0):
                trigger_alarm_beep(async_beep=True)

            return state, smooth_prob, self.unauth_counter

        return None

    def print_terminal_status(self, state):
        if state == "IDLE":
            color = Fore.CYAN + Style.BRIGHT
            print(f"{color} ⏸ IDLE          |  🔇 BUZZER: OFF {Style.RESET_ALL}")
        elif state == "ALARM":
            color = Back.RED + Fore.WHITE + Style.BRIGHT
            print(f"{color} 🚨 UNAUTHORIZED  |  🔊 BUZZER: ON (BUZZING) 🚨 {Style.RESET_ALL}")
        else:
            color = Fore.GREEN + Style.BRIGHT
            print(f"{color} 🟢 AUTHORIZED    |  🔇 BUZZER: OFF {Style.RESET_ALL}")


def run_live_forza_udp(engine: MinasLiveEngine, ip: str, port: int):
    """
    מאזין בזמן אמת לחבילות UDP מ-Forza Data Out (פורט ברירת מחדל: 5000)
    ודוגם בקצב מדויק של 20Hz לתוך המנוע הביומטרי תוך ריקון הבאפר למניעת השהיות.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((ip, port))
    sock.settimeout(0.5)

    print("=" * 92)
    print(f"=== Minas Live Forza UDP Guard Listening on {ip}:{port} (Sampling @ 20Hz) ===")
    print(f"=== Make sure Forza 'Data Out' is ON -> IP: 127.0.0.1, Port: {port}       ===")
    print("=" * 92)

    last_sample_time = 0.0
    connected_announced = False
    try:
        while True:
            try:
                data, _ = sock.recvfrom(1024)
            except socket.timeout:
                if engine.hud:
                    engine.hud.update_view("IDLE")
                continue

            # ריקון כל החבילות שהצטברו בבאפר של מערכת ההפעלה כדי לקרוא תמיד את החבילה העדכנית ביותר בזמן אמת
            latest_data = data
            sock.setblocking(False)
            while True:
                try:
                    pkt, _ = sock.recvfrom(1024)
                    latest_data = pkt
                except (BlockingIOError, socket.error):
                    break
            sock.settimeout(0.5)

            if not connected_announced:
                connected_announced = True
                print(f"{Fore.GREEN}[*] CONNECTED! Receiving Forza UDP telemetry packets (size: {len(latest_data)} bytes).{Style.RESET_ALL}")

            now = time.perf_counter()
            if now - last_sample_time < DT:
                continue

            _, th, br, st, speed_mps = parse_forza_udp_packet(latest_data)

            last_sample_time = now
            engine.push_sample(th, br, st, sim_tag="LIVE FORZA", speed_mps=speed_mps)

    except KeyboardInterrupt:
        print("\n[*] Stopping Live Forza Guard.")
    finally:
        sock.close()


def run_handover_demo(engine: MinasLiveEngine, duration_auth_sec=12.0, duration_unauth_sec=12.0, speed_multiplier=1.0):
    """
    מצב הדגמה חיה (PoC Video Demo):
    מזרים בזמן אמת (20Hz) את הנהיגה במסלול D (שלב 1: נהג מורשה - ירוק),
    ואז מדמה העברת שליטה לנהג לא-מורשה תוך כדי נסיעה — ותוך 4.0 שניות (8 חלונות סמוכים) מפעיל התרעה וזמזם!
    """
    script_dir = os.path.dirname(os.path.abspath(__file__))
    data_dir = os.path.join(script_dir, "Data")

    # חיפוש אוטומטי של קובץ מורשה וקובץ לא-מורשה במסלול D
    auth_candidates = [
        os.path.join(data_dir, "minas_Eyal_TrackD_Run1.csv"),
    ]
    unauth_candidates = [
        os.path.join(data_dir, "minas_Daniel_TrackD_Run3.csv"),
        os.path.join(data_dir, "minas_Rom_TrackD_Run4.csv"),
        os.path.join(data_dir, "minas_Shira_TrackD_Run2.csv"),
    ]
    auth_file = next((f for f in auth_candidates if os.path.isfile(f)), None)
    unauth_file = next((f for f in unauth_candidates if os.path.isfile(f)), None)

    if not auth_file or not unauth_file:
        import glob
        all_d = glob.glob(os.path.join(data_dir, "*TrackD*.csv"))
        for f in all_d:
            try:
                c_df = pd.read_csv(f, nrows=5)
                if c_df["is_authorized"].iloc[0] == 1 and not auth_file:
                    auth_file = f
                elif c_df["is_authorized"].iloc[0] == 0 and not unauth_file:
                    unauth_file = f
            except Exception:
                pass

    if not auth_file or not unauth_file:
        raise FileNotFoundError(f"Demo requires an Authorized and Unauthorized Track D file in '{data_dir}'.")

    df_auth = pd.read_csv(auth_file)
    df_unauth = pd.read_csv(unauth_file)

    # בחירת מקטע נהיגה פעיל של הנהג המורשה ושל הנהג הלא-מורשה ממסלול המבחן D
    n_auth = int(duration_auth_sec * SAMPLING_RATE_HZ)
    best_start_a = 160
    highest_prob = 0.0
    class_idx = list(engine.active_model.classes_).index(1)
    for cand_idx in range(120, min(len(df_auth) - n_auth, 2000), 40):
        win = df_auth.iloc[cand_idx : cand_idx + WINDOW_SIZE][["throttle", "brake", "steering", "d_throttle", "d_steering"]].values
        feats = engine.scaler.transform(extract_single_window_features(win))
        p = float(engine.active_model.predict_proba(feats)[0, class_idx])
        if p > highest_prob:
            highest_prob = p
            best_start_a = cand_idx
    slice_auth = df_auth.iloc[best_start_a : best_start_a + n_auth]

    n_unauth = int(duration_unauth_sec * SAMPLING_RATE_HZ)
    best_start_u = 400
    lowest_prob = 1.0
    for cand_idx in range(100, len(df_unauth) - n_unauth, 80):
        win = df_unauth.iloc[cand_idx : cand_idx + WINDOW_SIZE][["throttle", "brake", "steering", "d_throttle", "d_steering"]].values
        feats = engine.scaler.transform(extract_single_window_features(win))
        p = float(engine.active_model.predict_proba(feats)[0, class_idx])
        if p < lowest_prob:
            lowest_prob = p
            best_start_u = cand_idx

    slice_unauth = df_unauth.iloc[best_start_u : best_start_u + n_unauth]

    print("=" * 92)
    print("=== Minas Live PoC Simulation: Continuous Biometric Driver Authentication ===")
    print(f"=== Phase 1 ({duration_auth_sec:.0f}s): Authorized Driver   -> Expect GREEN (Authorized)              ===")
    print(f"=== Phase 2 ({duration_unauth_sec:.0f}s): Unauthorized Driver -> Expect RED   (Unauthorized in 4.0s!) ===")
    print("=" * 92)

    sleep_dt = DT / max(speed_multiplier, 0.1)

    try:
        print(f"\n{Fore.CYAN}{Style.BRIGHT}>>> [STAGE 1] Authorized driver is driving on Track D...{Style.RESET_ALL}")
        for _, row in slice_auth.iterrows():
            engine.push_sample(
                float(row["throttle"]),
                float(row["brake"]),
                float(row["steering"]),
                float(row["d_throttle"]),
                float(row["d_steering"]),
                sim_tag="Authorized",
            )
            time.sleep(sleep_dt)

        print(f"\n{Back.MAGENTA}{Fore.WHITE}{Style.BRIGHT}>>> [DRIVER SWITCH] Unauthorized driver takes control of the vehicle! <<<{Style.RESET_ALL}\n")
        # איפוס היסטוריית ההחלקה במעבר החד כדי להדגים תגובה בזמן אמת של 8 חלונות סמוכים (4.0 שניות)
        engine.prob_history.clear()

        for _, row in slice_unauth.iterrows():
            engine.push_sample(
                float(row["throttle"]),
                float(row["brake"]),
                float(row["steering"]),
                float(row["d_throttle"]),
                float(row["d_steering"]),
                sim_tag="Unauthorized",
            )
            time.sleep(sleep_dt)

        print("\n" + "=" * 92)
        print("[v] Live Handover PoC Demo Finished Successfully!")
        print("=" * 92)

    except KeyboardInterrupt:
        print("\n[*] Demo stopped by user.")


def main():
    parser = argparse.ArgumentParser(description="Minas Real-Time Biometric Driver Authentication (Forza UDP & Live Demo)")
    parser.add_argument("--mode", choices=["forza", "demo"], default=None, help="'forza' for live UDP port 5000, 'demo' for live handover simulation")
    parser.add_argument("--ip", default="127.0.0.1", help="UDP bind IP for Forza Data Out (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=5000, help="UDP port for Forza Data Out (default: 5000)")
    parser.add_argument("--threshold", type=float, default=None, help="Override decision threshold (e.g. 0.45)")
    parser.add_argument("--model", choices=["nn", "rf"], default="nn", help="Biometric model to run: 'nn' (Tiny Neural Net, default) or 'rf' (Random Forest)")
    parser.add_argument("--no-gui", action="store_true", help="Disable floating Tkinter HUD window")
    parser.add_argument("--speed", type=float, default=1.0, help="Playback speed multiplier for demo mode (default: 1.0 = real-time)")
    args = parser.parse_args()

    bundle = load_or_train_model()
    engine = MinasLiveEngine(bundle, use_gui=not args.no_gui, custom_threshold=args.threshold, model_type=args.model)

    mode = args.mode
    if mode is None:
        print("\nSelect Live Operation Mode:")
        print("  [1] Live Forza Horizon / Motorsport UDP Stream (Port 5000)")
        print("  [2] Live Handover Demo (Track D with Real-Time Alarm & HUD)")
        choice = input("Enter choice (1 or 2, default=2): ").strip()
        mode = "forza" if choice == "1" else "demo"

    if mode == "forza":
        run_live_forza_udp(engine, args.ip, args.port)
    else:
        run_handover_demo(engine, duration_auth_sec=10.0, duration_unauth_sec=10.0, speed_multiplier=args.speed)


if __name__ == "__main__":
    main()
