#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <FS.h>
#include <SD_MMC.h>
#include <ESP32Servo.h>
#include <ps5Controller.h>
#include "Config.h"

// Ensure FIRMWARE_VERSION is always defined even if build flags vary
#ifndef FIRMWARE_VERSION
#ifdef MINAS_FW_VERSION
#define FIRMWARE_VERSION MINAS_FW_VERSION
#else
#define FIRMWARE_VERSION "minas-10min-no-sonar-v3"
#endif
#endif

namespace {

// ============================================================================
// HARDWARE OBJECTS & EXPERIMENTAL STATE
// ============================================================================
Servo steeringServo;  // Steering actuator PWM driver instance
Servo esc;            // Motor Electronic Speed Controller PWM driver instance
File trialFile;       // Active CSV trial log file on the MicroSD card

bool sdReady = false;             // True when MicroSD card and /trials directory are available
bool trialActive = false;         // True while a 10-minute trial segment is being recorded
bool ownerLabel = false;          // True for 'owner' segment, false for 'nonowner' segment

uint32_t trialNumber = 0;         // Ascending global segment counter across sessions
uint32_t sampleSequence = 0;      // Monotonic sample sequence number within the active trial
uint32_t samplesSinceFlush = 0;   // Counter for periodic SD card buffer flushes
uint32_t trialStartMs = 0;        // Timestamp (millis) when current segment started
uint32_t lastSampleMs = 0;        // Timestamp (millis) of the previous 20 Hz control loop iteration

String trialPath;                 // Full path to the active trial file (e.g. "/trials/owner_segment_00001.csv")

float currentNormThrottle = 0.0f;
float currentNormBrake = 0.0f;
float currentNormSteering = 0.0f;
float currentDThrottle = 0.0f;
float currentDSteering = 0.0f;

float previousNormSteering = 0.0f;              // Prior normalized steering (-1.0 to +1.0)
float previousNormThrottle = 0.0f;              // Prior normalized throttle (0.0 to 1.0)
uint32_t previousSampleMs = 0;                 // Timestamp (millis) of prior sample for exact dt derivation

char currentTrack = DEFAULT_TRACK;              // Active driving track: 'A', 'B', or 'D'

bool previousCircle = false;      // Previous state of PS5 Circle button (for edge detection)
bool previousSquare = false;      // Previous state of PS5 Square button (for edge detection)
bool previousCross = false;       // Previous state of PS5 Cross button (for edge detection)
bool previousTriangle = false;    // Previous state of PS5 Triangle button (cycle track)
bool previousUp = false;          // Previous state of PS5 D-Pad Up (Track A)
bool previousRight = false;       // Previous state of PS5 D-Pad Right (Track B)
bool previousDown = false;        // Previous state of PS5 D-Pad Down (Track D)
bool previousLeft = false;        // Previous state of PS5 D-Pad Left (Track D)

/**
 * @brief Emits a clean tone without using or corrupting any hardware LEDC PWM timers.
 * Prevents timer hijacking that corrupts Servo and ESC frequencies.
 */
void safeBeep(uint32_t freqHz, uint32_t durationMs) {
    if (freqHz == 0 || durationMs == 0) return;
    uint32_t periodUs = 1000000UL / freqHz;
    uint32_t halfPeriodUs = periodUs / 2;
    uint32_t cycles = (durationMs * 1000UL) / periodUs;
    for (uint32_t i = 0; i < cycles; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delayMicroseconds(halfPeriodUs);
        digitalWrite(BUZZER_PIN, LOW);
        delayMicroseconds(halfPeriodUs);
    }
}

/**
 * @brief Audio confirmation of the selected track (1 beep=A, 2=B, 3=D).
 */
void beepTrack(char track) {
    int count = (track == 'A') ? 1 : (track == 'B') ? 2 : 3;
    for (int i = 0; i < count; i++) {
        safeBeep(2400, 60);
        if (i < count - 1) delay(70);
    }
}

/**
 * @brief Enforces safe neutral positions on both actuators.
 *
 * Centers the front steering wheels (90 degrees) and commands the ESC to its
 * neutral pulse width (1500 microseconds). Called during boot, segment finalization,
 * segment cancellation, controller disconnection, and I/O error states.
 */
void applyFailsafe() {
    steeringServo.write(STEERING_CENTER_DEG);
    esc.writeMicroseconds(ESC_FAILSAFE_US);
}

/**
 * @brief Maps a signed throttle percentage (-100% to +100%) to ESC pulse width (µs).
 *
 * Linear piecewise transformation:
 * - Negative values (-100% to 0%) map to reverse/braking (1000 µs to 1500 µs).
 * - Zero (0%) maps to neutral stop (1500 µs).
 * - Positive values (0% to +100%) map to forward acceleration (1500 µs to 2000 µs).
 *
 * @param throttlePercent Signed throttle value from -100.0 to +100.0.
 * @return Pulse width in microseconds constrained between ESC_MIN_US and ESC_MAX_US.
 */
int throttleToPulse(float throttlePercent) {
    if (fabsf(throttlePercent) < 1.0f) {
        return ESC_NEUTRAL_US;
    }
    throttlePercent = constrain(throttlePercent, -100.0f, 100.0f);
    if (throttlePercent >= 0.0f) {
        return ESC_NEUTRAL_US + static_cast<int>((ESC_MAX_US - ESC_NEUTRAL_US) * throttlePercent / 100.0f);
    }
    return ESC_NEUTRAL_US + static_cast<int>((ESC_NEUTRAL_US - ESC_MIN_US) * throttlePercent / 100.0f);
}

/**
 * @brief Returns a timestamp string formatted as YYYYMMDD_HHMMSS (if NTP synced)
 * or boot_HHhMMmSSs (based on system uptime since boot).
 */
String getTimestampString() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    char buf[40];
    if (timeinfo.tm_year > 100) { // Year > 2000 means clock was synchronized via NTP
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &timeinfo);
    } else {
        // Not synced with NTP: format based on uptime since boot
        uint32_t sec = millis() / 1000UL;
        uint32_t s = sec % 60;
        uint32_t m = (sec / 60) % 60;
        uint32_t h = sec / 3600;
        snprintf(buf, sizeof(buf), "boot_%02luh%02lum%02lus", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    }
    return String(buf);
}

/**
 * @brief Attempts a fast (up to 3.5s) NTP clock sync over Wi-Fi if credentials are provided in Config.h.
 * Immediately turns off the Wi-Fi radio upon completion to ensure zero RF interference with Bluetooth Classic.
 */
void syncTimeWithNTP() {
    if (!WIFI_NTP_ENABLED || strlen(WIFI_SSID) == 0) {
        Serial.println("[TIME] Wi-Fi SSID not configured; using system uptime for file timestamps.");
        return;
    }

    Serial.printf("[TIME] Connecting to Wi-Fi '%s' for NTP clock synchronization...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    const uint32_t startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < 3500UL)) {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[TIME] Wi-Fi connected! Querying NTP network time server...");
        configTime(NTP_TIMEZONE_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        struct tm timeinfo;
        if (getLocalTime(&timeinfo, 2000)) {
            char timeStr[40];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            Serial.printf("[TIME] Clock synchronized successfully: %s\n", timeStr);
        } else {
            Serial.println("[TIME] NTP server response timeout. Falling back to uptime.");
        }
    } else {
        Serial.println("[TIME] Wi-Fi connection timed out. Falling back to uptime.");
    }

    // Shut down Wi-Fi radio completely to free radio hardware for PS5 Bluetooth
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(50);
}

// ============================================================================
// MICROSD LOGGING SUBSYSTEM
// ============================================================================
/**
 * @brief Scans the SD card log directory to find the next sequential segment number.
 *
 * Reads existing filenames in SD_LOG_DIRECTORY matching "*segment_XXXXX.csv"
 * and returns the maximum observed index plus one. Ensures continuous numbering
 * across owner/nonowner sessions and reboots without overwriting existing files.
 *
 * @return The next available global segment sequence number (starting at 1).
 */
uint32_t nextSegmentNumber() {
    uint32_t highest = 0;
    File dir = SD_MMC.open(SD_LOG_DIRECTORY);
    if (!dir || !dir.isDirectory()) return 1;

    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            const char* marker = strstr(file.name(), "Run");
            if (!marker) marker = strstr(file.name(), "segment_");
            if (marker) {
                unsigned long number = 0;
                if ((sscanf(marker, "Run%lu", &number) == 1 || sscanf(marker, "segment_%lu", &number) == 1) && number > highest) {
                    highest = number;
                }
            }
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    return highest + 1;
}

/**
 * @brief Finalizes and saves a successfully completed 10-minute trial segment.
 *
 * Flushes all remaining buffers, closes the CSV file, engages actuator failsafe,
 * and emits a high-pitched victory chime (2500 Hz for 300 ms) confirming that
 * the full 10-minute trial is complete and preserved on the SD card.
 */
void finalizeTrial() {
    if (!trialActive) return;

    const uint32_t elapsedMs = millis() - trialStartMs;
    const uint32_t elapsedSec = elapsedMs / 1000UL;

    if (trialFile) {
        trialFile.flush();
        trialFile.close();
    }
    trialActive = false;
    applyFailsafe();

    // High confirmation chime: 2500 Hz for 300 ms
    safeBeep(2500, 300);
    Serial.printf("[TRIAL] Completed and saved (full 10 min, %lu s): %s\n",
        (unsigned long)elapsedSec, trialPath.c_str());
}

/**
 * @brief Stops an incomplete trial segment before 10 minutes.
 *
 * PRESERVES all collected telemetry rows without deleting anything.
 * Closes the file and renames it with an '_under10min_XXmXXs' suffix so that partial
 * driving sessions are safely retained and easily identifiable on the SD card.
 * Emits a low warning buzz (700 Hz for 200 ms).
 */
void cancelTrial() {
    if (!trialActive) return;

    const uint32_t elapsedMs = millis() - trialStartMs;
    const uint32_t elapsedSec = elapsedMs / 1000UL;
    const uint32_t mins = elapsedSec / 60;
    const uint32_t secs = elapsedSec % 60;

    if (trialFile) {
        trialFile.flush();
        trialFile.close();
    }
    trialActive = false;
    applyFailsafe();

    // Do NOT delete! Rename file to include '_under10min_XXmXXs'
    String newPath = trialPath;
    if (newPath.endsWith(".csv")) {
        newPath = newPath.substring(0, newPath.length() - 4);
    }
    char suffix[48];
    snprintf(suffix, sizeof(suffix), "_under10min_%02lum%02lus.csv",
        (unsigned long)mins, (unsigned long)secs);
    newPath += String(suffix);

    const bool renamed = SD_MMC.rename(trialPath.c_str(), newPath.c_str());
    if (renamed) {
        trialPath = newPath;
    }

    // Low warning buzzer tone: 700 Hz for 200 ms
    safeBeep(700, 200);
    Serial.printf("[TRIAL] Stopped early (< 10 min, %lu s); PRESERVED as: %s (renamed=%s)\n",
        (unsigned long)elapsedSec, trialPath.c_str(), renamed ? "true" : "false");
}

/**
 * @brief Opens a new trial CSV file and writes self-describing metadata headers.
 *
 * Initializes the trial state variables, generates a unique filename with sequential
 * segment number and date/time timestamp, writes ground-truth metadata, and emits
 * an audio confirmation tone.
 *
 * @param owner True for an 'owner' trial; false for a 'nonowner' trial.
 * @return True if the file was created and initialized successfully; false otherwise.
 */
bool openTrial(bool owner) {
    if (trialActive) return false;
    if (!sdReady) {
        Serial.println("[SD] ERROR: Cannot start trial - MicroSD card is not ready or failed to mount!");
        safeBeep(500, 400); // Low warning buzz
        return false;
    }

    ownerLabel = owner;
    trialNumber = nextSegmentNumber();
    const char* label = ownerLabel ? "owner" : "nonowner";
    const char* driverName = ownerLabel ? DRIVER_NAME_AUTHORIZED : DRIVER_NAME_UNAUTHORIZED;
    const String timestampStr = getTimestampString();

    char fileName[128];
    snprintf(fileName, sizeof(fileName), "%s/minas_%s_Track%c_Run%02lu_%s.csv",
        SD_LOG_DIRECTORY, driverName, currentTrack, static_cast<unsigned long>(trialNumber), timestampStr.c_str());
    trialPath = String(fileName);

    trialFile = SD_MMC.open(trialPath, FILE_WRITE);
    if (!trialFile) {
        Serial.printf("[SD] ERROR: Failed to create %s\n", trialPath.c_str());
        return false;
    }

    // Write CSV data column headers (all 10 telemetry columns exactly matching Forza)
    trialFile.println("timestamp_s,driver_name,is_authorized,track,run_number,throttle,brake,steering,d_throttle,d_steering");
    trialFile.flush();

    // Reset trial counters and delta tracking baselines
    sampleSequence = 0;
    samplesSinceFlush = 0;
    trialStartMs = millis();
    previousSampleMs = trialStartMs;
    previousNormSteering = 0.0f;
    previousNormThrottle = 0.0f;
    currentNormThrottle = 0.0f;
    currentNormBrake = 0.0f;
    currentNormSteering = 0.0f;
    currentDThrottle = 0.0f;
    currentDSteering = 0.0f;
    trialActive = true;

    // Chime confirmation: 2200 Hz for owner, 1200 Hz for nonowner
    safeBeep(ownerLabel ? 2200 : 1200, 150);
    Serial.printf("[TRIAL] Started %s (%s, Track %c, Run %lu); target duration=10 minutes\n",
        trialPath.c_str(),
        driverName,
        currentTrack,
        static_cast<unsigned long>(trialNumber));
    return true;
}

/**
 * @brief Writes a single telemetry sample row to the open trial CSV file.
 *
 * Formats all 10 columns exactly matching Forza data logging:
 * timestamp_s, driver_name, is_authorized, track, run_number, throttle, brake, steering, d_throttle, d_steering.
 */
void writeSample(float timestamp_s, const char* driverName, int isAuthorized,
                 char track, uint32_t runNumber,
                 float throttle, float brake, float steering,
                 float dThrottle, float dSteering) {
    if (!trialActive || !trialFile) return;

    const size_t written = trialFile.printf(
        "%.3f,%s,%d,%c,%lu,%.4f,%.4f,%.4f,%.4f,%.4f\n",
        timestamp_s,
        driverName,
        isAuthorized,
        track,
        static_cast<unsigned long>(runNumber),
        throttle,
        brake,
        steering,
        dThrottle,
        dSteering);

    if (written == 0) {
        Serial.println("[SD] ERROR: Write failure; cancelling trial and resetting SD");
        cancelTrial();
        sdReady = false;
        return;
    }

    if (++samplesSinceFlush >= SD_FLUSH_EVERY_N_SAMPLES) {
        trialFile.flush();
        samplesSinceFlush = 0;
    }
}

// ============================================================================
// PERIODIC CONTROLLER SAMPLING & CONTROL LOOP
// ============================================================================
/**
 * @brief Evaluates controller state, updates vehicle actuators, and logs data.
 *
 * Executed at a fixed 20 Hz rate (every 50 ms):
 * 1. Checks controller connection and button press transitions.
 * 2. Starts new trials when Circle (owner) or Square (nonowner) is pressed.
 * 3. Finalizes or cancels trials when Cross is pressed.
 * 4. Automatically completes and saves trials upon reaching 10 minutes.
 * 5. Handles controller disconnections safely (failsafe actuators + failsafe log row).
 * 6. Reads analog inputs, applies transfer functions, outputs actuator commands,
 *    and logs the sample to MicroSD.
 */
void sampleController() {
    const uint32_t now = millis();
    const bool connected = ps5.isConnected();
    const bool circle = connected && ps5.Circle();
    const bool square = connected && ps5.Square();
    const bool cross  = connected && ps5.Cross();
    const bool triangle = connected && ps5.Triangle();
    const bool dpadUp   = connected && ps5.Up();
    const bool dpadRight= connected && ps5.Right();
    const bool dpadDown = connected && ps5.Down();
    const bool dpadLeft = connected && ps5.Left();

    // 1. Session start & track selection triggers (when no trial is active)
    if (!trialActive) {
        if (circle && !previousCircle) openTrial(true);         // Circle = Owner trial
        else if (square && !previousSquare) openTrial(false);    // Square = Non-owner trial
        else if (triangle && !previousTriangle) {
            // Cycle track: A -> B -> D -> A
            currentTrack = (currentTrack == 'A') ? 'B' : (currentTrack == 'B') ? 'D' : 'A';
            Serial.printf("[TRACK] Switched to Track %c\n", currentTrack);
            beepTrack(currentTrack);
        }
        else if (dpadUp && !previousUp) {
            currentTrack = 'A';
            Serial.println("[TRACK] Selected Track A (Open Course)");
            beepTrack('A');
        }
        else if (dpadRight && !previousRight) {
            currentTrack = 'B';
            Serial.println("[TRACK] Selected Track B (Slalom S-Curves)");
            beepTrack('B');
        }
        else if ((dpadDown && !previousDown) || (dpadLeft && !previousLeft)) {
            currentTrack = 'D';
            Serial.println("[TRACK] Selected Track D (Held-Out Test)");
            beepTrack('D');
        }
    } else {
        // 2. Manual session halt (Cross button)
        if (cross && !previousCross) {
            if (millis() - trialStartMs >= TRIAL_DURATION_MS) {
                finalizeTrial();
            } else {
                cancelTrial();
            }
        }
        // 3. Automatic 10-minute completion check
        else if (millis() - trialStartMs >= TRIAL_DURATION_MS) {
            finalizeTrial();
        }
    }

    previousCircle = circle;
    previousSquare = square;
    previousCross = cross;
    previousTriangle = triangle;
    previousUp = dpadUp;
    previousRight = dpadRight;
    previousDown = dpadDown;
    previousLeft = dpadLeft;

    // 4. Controller disconnect failsafe
    if (!connected) {
        applyFailsafe();
        if (trialActive) {
            const float timestamp_s = static_cast<float>(now - trialStartMs) / 1000.0f;
            const char* driverName = ownerLabel ? DRIVER_NAME_AUTHORIZED : DRIVER_NAME_UNAUTHORIZED;
            const int isAuthorized = ownerLabel ? 1 : 0;
            writeSample(timestamp_s, driverName, isAuthorized, currentTrack, trialNumber, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            sampleSequence++;
            previousNormSteering = 0.0f;
            previousNormThrottle = 0.0f;
            currentNormThrottle = 0.0f;
            currentNormBrake = 0.0f;
            currentNormSteering = 0.0f;
            currentDThrottle = 0.0f;
            currentDSteering = 0.0f;
            previousSampleMs = now;
        }
        return;
    }

    // 5. Read Analog Inputs from PS5 DualSense
    const int rawLx = ps5.LStickX();  // Steering: Left Stick horizontal axis (-128 to 127)
    const int rawL2 = ps5.L2Value();  // Left Trigger: Brake (0 to 255)
    const int rawR2 = ps5.R2Value();  // Right Trigger: Throttle (0 to 255)

    // Deadband thresholds to eliminate stick drift, trigger resting noise,
    // and accidental trigger activation when controller rests on a desk
    constexpr int TRIGGER_DEADBAND = 20;   // 0..255 (~8% threshold)
    constexpr int STEERING_DEADBAND = 10;  // -128..127 (~8% threshold)

    // Smooth linear deadband mapping: subtracts threshold so controls start smoothly from center
    int filteredLx = 0;
    if (rawLx > STEERING_DEADBAND) {
        filteredLx = (rawLx - STEERING_DEADBAND) * 128 / (128 - STEERING_DEADBAND);
    } else if (rawLx < -STEERING_DEADBAND) {
        filteredLx = (rawLx + STEERING_DEADBAND) * 128 / (128 - STEERING_DEADBAND);
    }

    const int filteredR2 = (rawR2 > TRIGGER_DEADBAND) ? (rawR2 - TRIGGER_DEADBAND) : 0;
    const int filteredL2 = (rawL2 > TRIGGER_DEADBAND) ? (rawL2 - TRIGGER_DEADBAND) : 0;

    // 1) Normalized Steering: -1.0 (full left) to +1.0 (full right), 0.0 centered
    const float normSteering = constrain(static_cast<float>(filteredLx) / 128.0f, -1.0f, 1.0f);

    // 2) Normalized Throttle: 0.0 to 1.0
    const float normThrottle = (filteredR2 > 0) ?
        constrain(static_cast<float>(filteredR2) / (255.0f - TRIGGER_DEADBAND), 0.0f, 1.0f) : 0.0f;

    // 3) Normalized Brake: 0.0 to 1.0
    const float normBrake = (filteredL2 > 0) ?
        constrain(static_cast<float>(filteredL2) / (255.0f - TRIGGER_DEADBAND), 0.0f, 1.0f) : 0.0f;

    // 4) Compute Actuator Commands for Physical Vehicle
    // Steering servo: 0° (full left), 90° (center), 180° (full right)
    const int steeringCommand = constrain(
        static_cast<int>(roundf(STEERING_CENTER_DEG + (normSteering * (STEERING_MAX_DEG - STEERING_CENTER_DEG)))),
        STEERING_MIN_DEG, STEERING_MAX_DEG);

    // ESC pulse: net throttle percent (-100% full brake/reverse to +100% full forward)
    const float netThrottlePercent = (normThrottle - normBrake) * 100.0f;
    const int escCommand = throttleToPulse(netThrottlePercent);

    // Command physical vehicle actuators
    steeringServo.write(steeringCommand);
    esc.writeMicroseconds(ENABLE_MOTOR_OUTPUT ? escCommand : ESC_FAILSAFE_US);

    // 5) Calculate time derivatives (d_throttle and d_steering)
    float dt = (previousSampleMs > 0 && now > previousSampleMs) ?
        (static_cast<float>(now - previousSampleMs) / 1000.0f) : 0.05f;
    if (dt <= 0.001f) dt = 0.05f;

    float dThrottle = 0.0f;
    float dSteering = 0.0f;
    if (sampleSequence > 0) {
        dThrottle = (normThrottle - previousNormThrottle) / dt;
        dSteering = (normSteering - previousNormSteering) / dt;
    }

    // 6) MicroSD Data Logging - active during recorded trials (all 10 columns matching Forza)
    if (trialActive) {
        const float timestamp_s = static_cast<float>(now - trialStartMs) / 1000.0f;
        const char* driverName = ownerLabel ? DRIVER_NAME_AUTHORIZED : DRIVER_NAME_UNAUTHORIZED;
        const int isAuthorized = ownerLabel ? 1 : 0;

        writeSample(timestamp_s, driverName, isAuthorized, currentTrack, trialNumber,
                    normThrottle, normBrake, normSteering,
                    dThrottle, dSteering);
        sampleSequence++;
    }

    currentNormThrottle = normThrottle;
    currentNormBrake = normBrake;
    currentNormSteering = normSteering;
    currentDThrottle = dThrottle;
    currentDSteering = dSteering;

    previousNormSteering = normSteering;
    previousNormThrottle = normThrottle;
    previousSampleMs = now;
}

} // anonymous namespace

// ============================================================================
// ARDUINO SETUP ROUTINE
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[INIT] Minas 10-minute driver data collector (without sonar)");

    // Configure buzzer output
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Allocate all 4 hardware LEDC timers for PWM (prevents conflicts between Servo, ESC, and tone)
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    ESP32PWM::allocateTimer(2);
    ESP32PWM::allocateTimer(3);

    // Attach Servo and ESC to PWM channels and enforce neutral failsafe immediately
    steeringServo.setPeriodHertz(50);
    steeringServo.attach(STEERING_SERVO_PIN, SERVO_MIN_US, SERVO_MAX_US);
    esc.setPeriodHertz(50);
    esc.attach(ESC_PIN, ESC_MIN_US, ESC_MAX_US);
    applyFailsafe();
    Serial.println("[ESC] Arming ESC with neutral pulse (2-second hold)...");
    delay(2000);

    // Initialize MicroSD card in 1-bit SDMMC mode (format_if_mount_failed = false to protect data)
    if (!SD_MMC.begin(SD_MOUNT_POINT, true, false)) {
        Serial.println("[SD] ERROR: Initialization failed. Check card insertion and FAT32 format.");
    } else {
        if (!SD_MMC.exists(SD_LOG_DIRECTORY)) {
            SD_MMC.mkdir(SD_LOG_DIRECTORY);
        }
        sdReady = SD_MMC.exists(SD_LOG_DIRECTORY);
        Serial.printf("[SD] Ready=%s; Log directory: %s\n",
            sdReady ? "true" : "false", SD_LOG_DIRECTORY);
    }

    // Optional: Synchronize real-world date & time via Wi-Fi NTP before Bluetooth begins
    syncTimeWithNTP();

    // Check if controller MAC address is still using default dummy value
    if (strcmp(PS5_CONTROLLER_MAC, "00:00:00:00:00:00") == 0) {
        Serial.println("[PS5] WARNING: PS5_CONTROLLER_MAC in Config.h is set to '00:00:00:00:00:00'.");
        Serial.println("[PS5] Please update PS5_CONTROLLER_MAC with your controller's Bluetooth MAC address!");
    }

    // Initialize PS5 Bluetooth Classic stack
    Serial.printf("[PS5] Initializing Bluetooth with target MAC: %s\n", PS5_CONTROLLER_MAC);
    ps5.begin(PS5_CONTROLLER_MAC);
    Serial.println("[PS5] Bluetooth host ready. Turn on your PS5 DualSense controller.");

    Serial.println("[READY] Protocol:");
    Serial.printf("  - Circle Button:    Start 10-minute AUTHORIZED segment ('%s', is_authorized=1)\n", DRIVER_NAME_AUTHORIZED);
    Serial.printf("  - Square Button:    Start 10-minute UNAUTHORIZED segment ('%s', is_authorized=0)\n", DRIVER_NAME_UNAUTHORIZED);
    Serial.printf("  - D-Pad / Triangle: Select Track A/B/D (Active: Track %c)\n", currentTrack);
    Serial.println("  - Cross Button:     Stop & Preserve (< 10 min) or Finish (>= 10 min)");
    Serial.println("  - Auto-stop:        Automatically saves at exactly 10 minutes");
    Serial.printf("[READY] Motor Output: %s\n", ENABLE_MOTOR_OUTPUT ? "ENABLED (Live)" : "BENCH (Neutral)");
}

// ============================================================================
// ARDUINO MAIN LOOP
// ============================================================================
void loop() {
    const uint32_t now = millis();

    // Track connection transitions for immediate user feedback
    static bool wasConnected = false;
    const bool currentConnected = ps5.isConnected();
    if (currentConnected && !wasConnected) {
        wasConnected = true;
        Serial.println("\n[PS5] >>> CONTROLLER CONNECTED & READY! <<<");
        safeBeep(1800, 100);
    } else if (!currentConnected && wasConnected) {
        wasConnected = false;
        Serial.println("\n[PS5] >>> CONTROLLER DISCONNECTED! (Failsafe Active) <<<");
        safeBeep(600, 150);
        applyFailsafe();
    }

    // Periodic status print every 2 seconds when controller is connected and trial is active
    static uint32_t lastStatusPrintMs = 0;
    if (trialActive && (now - lastStatusPrintMs >= 2000UL)) {
        lastStatusPrintMs = now;
        const uint32_t elapsedMs = now - trialStartMs;
        const uint32_t elapsedSec = elapsedMs / 1000UL;
        const uint32_t remainingSec = (elapsedMs < TRIAL_DURATION_MS) ? ((TRIAL_DURATION_MS - elapsedMs) / 1000UL) : 0UL;
        Serial.printf("[TRIAL PROGRESS] %s | Track: %c | Elapsed: %lu s (%lu min) | Remaining: %lu s | Samples: %lu\n",
                      ownerLabel ? DRIVER_NAME_AUTHORIZED : DRIVER_NAME_UNAUTHORIZED,
                      currentTrack,
                      (unsigned long)elapsedSec, (unsigned long)(elapsedSec / 60),
                      (unsigned long)remainingSec, (unsigned long)sampleSequence);
    }

    // High-visibility live control telemetry printed every 500 ms when controller is connected
    static uint32_t lastTelemetryPrintMs = 0;
    if (currentConnected && (now - lastTelemetryPrintMs >= 500UL)) {
        lastTelemetryPrintMs = now;
        Serial.printf("[LIVE CTRL] Track:%c | Run:%lu | Throt:%.2f | Brk:%.2f | Steer:%+.2f | dThrot:%+.2f | dSteer:%+.2f | State:%s\n",
                      currentTrack,
                      static_cast<unsigned long>(trialNumber),
                      currentNormThrottle, currentNormBrake, currentNormSteering,
                      currentDThrottle, currentDSteering,
                      trialActive ? (ownerLabel ? "RECORDING_AUTHORIZED" : "RECORDING_UNAUTHORIZED") : "STANDBY_NEUTRAL");
    }

    // Execute controller sampling at a strict 20 Hz (50 ms) fixed rate without phase drift
    if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
        lastSampleMs += SAMPLE_INTERVAL_MS;
        if (now - lastSampleMs > SAMPLE_INTERVAL_MS) {
            // Resynchronize if execution fell significantly behind
            lastSampleMs = now;
        }
        sampleController();
    }

    // Safety guarantee: stop motors immediately if controller disconnects
    if (!currentConnected) {
        applyFailsafe();
    }

    delay(1); // Yield execution to background FreeRTOS tasks (Bluetooth stack)
}
