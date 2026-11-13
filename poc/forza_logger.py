import socket
import time
import struct
import pandas as pd
import os

# הגדרות תקשורת ל-Forza
UDP_IP = "127.0.0.1"
UDP_PORT = 5000

# הגדרות דגימה של פרויקט Minas
SAMPLING_RATE_HZ = 20
DT = 1.0 / SAMPLING_RATE_HZ
DURATION_SECONDS = 10 * 60  # 10 דקות (600 שניות)

def record_telemetry():
    print("=== Minas Project: Forza Telemetry Logger ===")
    # איסוף מטא-דאטה לפני תחילת הנסיעה
    driver_name = input("1. Enter Driver Name (e.g., Eyal, Guest1): ").strip()
    is_auth = input(f"2. Is {driver_name} the Authorized driver? (1 = Yes, 0 = No): ").strip()
    track_name = input("3. Enter Track (A, B, or D): ").strip().upper()
    run_num = input("4. Enter Run Number (e.g., 1, 2, 3): ").strip()
    
    # פתיחת שקע UDP
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.setblocking(False)

    data_log = []
    print(f"\n--- Starting 10-Minute Recording ---")
    print(f"Driver: {driver_name} | Track: {track_name} | Run: {run_num}")
    print("Go to Forza and start driving! The script will auto-stop in 10 minutes...")
    
    start_time = time.time()
    next_sample_time = start_time

    try:
        while True:
            current_time = time.time()
            elapsed = current_time - start_time

            # סיום אוטומטי לאחר 10 דקות בדיוק
            if elapsed >= DURATION_SECONDS:
                print("\n[+] 10 minutes reached. Stopping recording.")
                break

            # דגימה ב-20Hz בלבד
            if current_time >= next_sample_time:
                latest_data = None
                
                # ריקון באפר כדי לקחת רק את המנה המעודכנת ביותר
                while True:
                    try:
                        data, addr = sock.recvfrom(1024)
                        latest_data = data
                    except BlockingIOError:
                        break

                if latest_data and len(latest_data) >= 311:
                    # חילוץ נתונים מפורמט Forza Dash
                    accel_raw = latest_data[315]
                    brake_raw = latest_data[316]
                    steer_raw = struct.unpack('b', bytes([latest_data[320]]))[0]

                    # נרמול הנתונים
                    throttle = accel_raw / 255.0
                    brake = brake_raw / 255.0
                    steering = steer_raw / 127.0

                    data_log.append({
                        "timestamp_s": round(elapsed, 3),
                        "driver_name": driver_name,
                        "is_authorized": int(is_auth),
                        "track": track_name,
                        "run_number": int(run_num),
                        "throttle": round(throttle, 4),
                        "brake": round(brake, 4),
                        "steering": round(steering, 4)
                    })
                
                next_sample_time += DT

    except KeyboardInterrupt:
        print("\n[!] Recording manually interrupted by user.")

    finally:
        sock.close()
        
        # שמירת הנתונים לקובץ CSV ייעודי לנסיעה זו
        if len(data_log) > 0:
            df = pd.DataFrame(data_log)
            
            # חישוב נגזרות לפי הזמן
            df['d_throttle'] = df['throttle'].diff() / DT
            df['d_steering'] = df['steering'].diff() / DT
            df.fillna(0, inplace=True) # איפוס הנגזרת בשורה הראשונה
            
            # ארגון סדר העמודות הסופי שביקשת
            columns_order = [
                "timestamp_s", "driver_name", "is_authorized", "track", "run_number",
                "throttle", "brake", "steering", "d_throttle", "d_steering"
            ]
            df = df[columns_order]
            
            # יצירת שם קובץ דינמי
            filename = f"minas_{driver_name}_Track{track_name}_Run{run_num}.csv"
            
            # יצירת תיקיית Data אם לא קיימת
            if not os.path.exists("Data"):
                os.makedirs("Data")
            
            filepath = os.path.join("Data", filename)
            df.to_csv(filepath, index=False)
            
            print("\n--- Final Recorded Telemetry Data (Preview) ---")
            print(df.head(5))
            print(f"\n[v] Saved {len(df)} samples to {filepath}")
        else:
            print("\n[x] No data was recorded. Check Forza UDP settings and ensure the game is unpaused.")

if __name__ == "__main__":
    record_telemetry()
