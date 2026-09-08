#include <Arduino.h>
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

float previousSteering = STEERING_CENTER_DEG;  // Prior steering angle (used to compute steering_delta)
float previousThrottle = 0.0f;                 // Prior throttle percentage (used to compute throttle_delta)

bool previousCircle = false;      // Previous state of PS5 Circle button (for edge detection)
bool previousSquare = false;      // Previous state of PS5 Square button (for edge detection)
bool previousCross = false;       // Previous state of PS5 Cross button (for edge detection)

// ============================================================================
// ACTUATOR & FAILSAFE SUBSYSTEM
// ============================================================================
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
    throttlePercent = constrain(throttlePercent, -100.0f, 100.0f);
    if (throttlePercent >= 0.0f) {
        return ESC_NEUTRAL_US + static_cast<int>((ESC_MAX_US - ESC_NEUTRAL_US) * throttlePercent / 100.0f);
    }
    return ESC_NEUTRAL_US + static_cast<int>((ESC_NEUTRAL_US - ESC_MIN_US) * throttlePercent / 100.0f);
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
            const char* marker = strstr(file.name(), "segment_");
            if (marker) {
                unsigned long number = 0;
                if (sscanf(marker, "segment_%lu", &number) == 1 && number > highest) {
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
 * and emits a high-pitched victory chime (2500 Hz for 500 ms) confirming that
 * the full 10-minute trial is complete and preserved on the SD card.
 */
void finalizeTrial() {
    if (!trialActive) return;

    if (trialFile) {
        trialFile.flush();
        trialFile.close();
    }
    trialActive = false;
    applyFailsafe();

    // High confirmation chime: 2500 Hz for 500 ms
    tone(BUZZER_PIN, 2500, 500);
    Serial.printf("[TRIAL] Completed and saved: %s\n", trialPath.c_str());
}

/**
 * @brief Cancels and deletes an incomplete trial segment stopped before 10 minutes.
 *
 * If a session is halted before reaching the required 10-minute duration,
 * the partial CSV file is closed and deleted from the SD card to prevent
 * incomplete records from polluting the training dataset. Emits a low warning buzz
 * (700 Hz for 300 ms).
 */
void cancelTrial() {
    if (!trialActive) return;

    if (trialFile) {
        trialFile.flush();
        trialFile.close();
    }
    const bool removed = SD_MMC.remove(trialPath.c_str());
    trialActive = false;
    applyFailsafe();

    // Low warning buzzer tone: 700 Hz for 300 ms
    tone(BUZZER_PIN, 700, 300);
    Serial.printf("[TRIAL] Cancelled (< 10 min); deleted=%s path=%s\n",
        removed ? "true" : "false", trialPath.c_str());
}

/**
 * @brief Opens a new trial CSV file and writes self-describing metadata headers.
 *
 * Initializes the trial state variables, resets derivative tracking baselines,
 * creates the CSV file with ground-truth metadata, and emits an audio confirmation tone.
 *
 * @param owner True for an 'owner' trial; false for a 'nonowner' trial.
 * @return True if the file was created and initialized successfully; false otherwise.
 */
bool openTrial(bool owner) {
    if (!sdReady || trialActive) return false;

    ownerLabel = owner;
    trialNumber = nextSegmentNumber();
    const char* label = ownerLabel ? "owner" : "nonowner";

    char fileName[96];
    snprintf(fileName, sizeof(fileName), "%s/%s_segment_%05lu.csv",
        SD_LOG_DIRECTORY, label, static_cast<unsigned long>(trialNumber));
    trialPath = String(fileName);

    trialFile = SD_MMC.open(trialPath, FILE_WRITE);
    if (!trialFile) {
        Serial.printf("[SD] ERROR: Failed to create %s\n", trialPath.c_str());
        return false;
    }

    // Write self-describing metadata header lines
    trialFile.printf("schema_version=3\n");
    trialFile.printf("firmware_version=%s\n", FIRMWARE_VERSION);
    trialFile.printf("label=%s\n", ownerLabel ? "owner" : "nonowner");
    trialFile.printf("is_owner=%d\n", ownerLabel ? 1 : 0);
    trialFile.printf("sample_interval_ms=%lu\n", SAMPLE_INTERVAL_MS);
    trialFile.printf("planned_duration_ms=%lu\n", TRIAL_DURATION_MS);
    trialFile.println("features=controller_and_actuators_only");
    trialFile.println("---");

    // Write CSV data column headers (exactly 20 columns)
    trialFile.println(
        "segment_number,sample_sequence,timestamp_ms,elapsed_ms,label,is_owner,"
        "controller_connected,raw_lx,raw_ly,raw_rx,raw_ry,l2,r2,buttons_mask,"
        "steering_deg,throttle_percent,steering_command_deg,esc_command_us,"
        "steering_delta,throttle_delta");
    trialFile.flush();

    // Reset trial counters and delta tracking baselines
    sampleSequence = 0;
    samplesSinceFlush = 0;
    trialStartMs = millis();
    previousSteering = STEERING_CENTER_DEG;
    previousThrottle = 0.0f;
    trialActive = true;

    // Chime confirmation: 2200 Hz for owner, 1200 Hz for nonowner
    tone(BUZZER_PIN, ownerLabel ? 2200 : 1200, 180);
    Serial.printf("[TRIAL] Started %s (%s); target duration=10 minutes\n",
        trialPath.c_str(), ownerLabel ? "owner" : "nonowner");
    return true;
}

/**
 * @brief Encodes the physical PS5 buttons into an unsigned 16-bit bitmask.
 *
 * @return 16-bit integer where each bit corresponds to an individual button state.
 */
uint16_t buttonsMask() {
    uint16_t mask = 0;
    if (ps5.Cross())    mask |= (1u << 0);
    if (ps5.Circle())   mask |= (1u << 1);
    if (ps5.Square())   mask |= (1u << 2);
    if (ps5.Triangle()) mask |= (1u << 3);
    if (ps5.L1())       mask |= (1u << 4);
    if (ps5.R1())       mask |= (1u << 5);
    if (ps5.L3())       mask |= (1u << 6);
    if (ps5.R3())       mask |= (1u << 7);
    if (ps5.Up())       mask |= (1u << 8);
    if (ps5.Down())     mask |= (1u << 9);
    if (ps5.Left())     mask |= (1u << 10);
    if (ps5.Right())    mask |= (1u << 11);
    return mask;
}

/**
 * @brief Writes a single telemetry sample row to the open trial CSV file.
 *
 * Formats all 20 columns matching their exact data types. Flushes periodically
 * to balance write performance and data protection against power loss.
 */
void writeSample(uint32_t now, bool connected, int rawLx, int rawLy, int rawRx, int rawRy,
    int l2, int r2, uint16_t buttons, float steering, float throttle,
    int steeringCommand, int escCommand, float steeringDelta, float throttleDelta) {
    if (!trialActive || !trialFile) return;

    const size_t written = trialFile.printf(
        "%lu,%lu,%lu,%lu,%s,%d,%d,%d,%d,%d,%d,%d,%d,%u,%.2f,%.2f,%d,%d,%.4f,%.4f\n",
        static_cast<unsigned long>(trialNumber),
        static_cast<unsigned long>(++sampleSequence),
        static_cast<unsigned long>(now),
        static_cast<unsigned long>(now - trialStartMs),
        ownerLabel ? "owner" : "nonowner",
        ownerLabel ? 1 : 0,
        connected ? 1 : 0,
        rawLx, rawLy, rawRx, rawRy,
        l2, r2,
        static_cast<unsigned>(buttons),
        steering,
        throttle,
        steeringCommand,
        escCommand,
        steeringDelta,
        throttleDelta);

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

    // 1. Session start triggers (allowed only when no trial is currently running)
    if (!trialActive) {
        if (circle && !previousCircle) openTrial(true);         // Circle = Owner trial
        else if (square && !previousSquare) openTrial(false);    // Square = Non-owner trial
    }

    // 2. Manual session halt (Cross button)
    // If pressed after completing 10 minutes: finalize and save.
    // If pressed before 10 minutes: cancel and delete incomplete trial.
    if (trialActive && cross && !previousCross) {
        if (now - trialStartMs >= TRIAL_DURATION_MS) {
            finalizeTrial();
        } else {
            cancelTrial();
        }
    }

    previousCircle = circle;
    previousSquare = square;
    previousCross = cross;

    // If no trial is active, ensure vehicle remains stopped and return
    if (!trialActive) {
        applyFailsafe();
        return;
    }

    // 3. Automatic 10-minute completion check
    if (now - trialStartMs >= TRIAL_DURATION_MS) {
        finalizeTrial();
        return;
    }

    // 4. Controller disconnect failsafe during an active trial
    // Applies neutral output and logs a sample with connected=0 to preserve time continuity.
    if (!connected) {
        applyFailsafe();
        const float steeringDelta = STEERING_CENTER_DEG - previousSteering;
        const float throttleDelta = 0.0f - previousThrottle;
        writeSample(now, false, 0, 0, 0, 0, 0, 0, 0,
            STEERING_CENTER_DEG, 0.0f, STEERING_CENTER_DEG,
            ESC_FAILSAFE_US, steeringDelta, throttleDelta);
        previousSteering = STEERING_CENTER_DEG;
        previousThrottle = 0.0f;
        return;
    }

    // 5. Read Analog Inputs from PS5 DualSense
    const int rawLx = ps5.LStickX();  // Steering: Left Stick horizontal axis (-128 to 127)
    const int rawLy = ps5.LStickY();  // Left Stick vertical axis (-128 to 127)
    const int rawRx = ps5.RStickX();  // Right Stick horizontal axis (-128 to 127)
    const int rawRy = ps5.RStickY();  // Right Stick vertical axis (-128 to 127)
    const int l2 = ps5.L2Value();     // Left Analog Trigger: Brake / Reverse (0 to 255)
    const int r2 = ps5.R2Value();     // Right Analog Trigger: Forward Throttle (0 to 255)

    // Calculate floating-point steering angle (0.0° to 180.0°, centered at 90.0°)
    const float steering = ((static_cast<float>(rawLx) + 128.0f) *
        (STEERING_MAX_DEG - STEERING_MIN_DEG) / 255.0f) + STEERING_MIN_DEG;

    // Calculate signed net throttle percentage (-100.0% to +100.0%)
    const float throttle = (static_cast<float>(r2) - static_cast<float>(l2)) * 100.0f / 255.0f;

    // Calculate dynamic control rate-of-change (first derivative)
    const float steeringDelta = steering - previousSteering;
    const float throttleDelta = throttle - previousThrottle;

    // Compute integer actuator commands
    const int steeringCommand = constrain(static_cast<int>(roundf(steering)), STEERING_MIN_DEG, STEERING_MAX_DEG);
    const int escCommand = throttleToPulse(throttle);

    // 6. Actuator Output Commands
    steeringServo.write(steeringCommand);
    esc.writeMicroseconds(ENABLE_MOTOR_OUTPUT ? escCommand : ESC_FAILSAFE_US);

    // 7. Data Logging
    // Always records the intended escCommand even in bench mode (ENABLE_MOTOR_OUTPUT = false)
    writeSample(now, true, rawLx, rawLy, rawRx, rawRy, l2, r2, buttonsMask(),
        steering, throttle, steeringCommand, escCommand, steeringDelta, throttleDelta);

    previousSteering = steering;
    previousThrottle = throttle;
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

    // Attach Servo and ESC to PWM channels and enforce neutral failsafe immediately
    steeringServo.setPeriodHertz(50);
    steeringServo.attach(STEERING_SERVO_PIN, 500, 2500);
    esc.setPeriodHertz(50);
    esc.attach(ESC_PIN, ESC_MIN_US, ESC_MAX_US);
    applyFailsafe();

    // Initialize MicroSD card in 1-bit SDMMC mode
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

    // Initialize PS5 Bluetooth Classic stack
    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("[PS5] ERROR: Bluetooth host initialization failed. Check controller MAC in Config.h.");
    } else {
        Serial.println("[PS5] Bluetooth initialized. Pair your PS5 DualSense controller.");
    }

    Serial.println("[READY] Protocol:");
    Serial.println("  - Circle Button: Start 10-minute OWNER segment");
    Serial.println("  - Square Button: Start 10-minute NON-OWNER segment");
    Serial.println("  - Cross Button:  Cancel (< 10 min) or Finish (>= 10 min)");
    Serial.println("  - Auto-stop:     Automatically saves at exactly 10 minutes");
    Serial.printf("[READY] Motor Output: %s\n", ENABLE_MOTOR_OUTPUT ? "ENABLED (Live)" : "BENCH (Neutral)");
}

// ============================================================================
// ARDUINO MAIN LOOP
// ============================================================================
void loop() {
    const uint32_t now = millis();

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
    if (!ps5.isConnected()) {
        applyFailsafe();
    }

    delay(1); // Yield execution to background FreeRTOS tasks (Bluetooth stack)
}
