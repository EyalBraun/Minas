#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <ESP32Servo.h>
#include <ps5Controller.h>
#include "Config.h"

// Fallback firmware version tag if not supplied via build_flags in platformio.ini
#ifndef MINAS_FW_VERSION
#define MINAS_FW_VERSION "wrover-driver-collection-v1"
#endif

namespace {

// ============================================================================
// HARDWARE OBJECTS & EXPERIMENTAL STATE
// ============================================================================
Servo steeringServo;      // Steering actuator (ESP32 PWM channel)
Servo esc;                // Traction motor Electronic Speed Controller (ESP32 PWM channel)
File trialFile;           // Open CSV trial file handle on MicroSD card

bool sdReady = false;                           // True when MicroSD card and log directory are mounted
bool trialActive = false;                       // True while an active logging segment is being recorded
bool ownerLabel = (INITIAL_OWNER_LABEL != 0);   // Binary label: true = owner, false = nonowner
bool previousCircle = false;                    // Edge detection for PS5 Circle button
bool previousCross = false;                     // Edge detection for PS5 Cross button

uint32_t sampleSequence = 0;                    // Monotonic sample index within the current segment file
uint32_t trialNumber = 0;                       // Globally ascending segment number across trials
uint32_t samplesSinceFlush = 0;                 // Counter for periodic SD card flushing
uint32_t lastSampleMs = 0;                      // Timestamp of last 20 Hz control/logging iteration
uint32_t trialStartMs = 0;                      // Timestamp (millis) when current segment file was opened
uint32_t lastSonarMs = 0;                       // Timestamp of last ultrasonic sonar ping
int sonarDistanceCm = -1;                       // Current measured distance in cm (-1 = invalid/timeout)
String trialPath;                               // Absolute path of the active CSV file on MicroSD

float previousSteering = STEERING_CENTER_DEG;   // Previous steering angle (used to calculate steering_delta)
float previousThrottle = 0.0f;                  // Previous throttle percentage (used to calculate throttle_delta)

// ============================================================================
// ULTRASONIC SENSOR SUBSYSTEM (HC-SR04)
// ============================================================================
/**
 * @brief Trigger an ultrasonic pulse and measure return echo duration.
 * @return Distance in centimeters (2-450 cm), or -1 if no valid echo returned.
 *
 * Physics notes:
 * - Speed of sound in dry air at 20°C is ~343 m/s = 0.0343 cm/µs.
 * - Distance = (Duration * 0.0343) / 2 = Duration / 58.3 µs.
 * - Pulse timeout of 25 ms caps maximum range to ~430 cm and avoids long blocking delays.
 */
int readSonarCm() {
    digitalWrite(SONAR_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(SONAR_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SONAR_TRIG_PIN, LOW);

    const unsigned long duration = pulseIn(SONAR_ECHO_PIN, HIGH, SONAR_TIMEOUT_US);
    if (duration == 0) return -1; // Echo timed out (no obstacle within range)

    const int centimeters = static_cast<int>(duration / 58UL);
    return (centimeters >= 2 && centimeters <= 450) ? centimeters : -1;
}

// ============================================================================
// ACTUATOR & FAILSAFE SUBSYSTEM
// ============================================================================
/**
 * @brief Apply safe neutral positions to both actuators.
 * Centered steering (90°) and neutral throttle pulse (1500 µs).
 * Called during startup, controller disconnection, trial stops, and SD errors.
 */
void applyFailsafe() {
    steeringServo.write(STEERING_CENTER_DEG);
    esc.writeMicroseconds(ESC_FAILSAFE_US);
}

/**
 * @brief Map signed throttle percentage (-100% to +100%) to ESC pulse width (µs).
 * -100% maps to ESC_MIN_US (1000 µs - full reverse / brake).
 *     0% maps to ESC_NEUTRAL_US (1500 µs - stop / neutral).
 * +100% maps to ESC_MAX_US (2000 µs - full forward).
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
 * @brief Safely flush and close the currently active trial CSV file.
 */
void closeTrial() {
    if (!trialFile) return;
    trialFile.flush();
    trialFile.close();
    trialActive = false;
    applyFailsafe();
    Serial.printf("[TRIAL] Closed %s\n", trialPath.c_str());
}

/**
 * @brief Determine the next available global segment sequence number.
 * Scans existing files in SD_LOG_DIRECTORY to find the maximum segment number
 * so numbering continuously increases across owner / nonowner switches.
 */
uint32_t nextSegmentNumber() {
    uint32_t highest = 0;
    File dir = SD_MMC.open(SD_LOG_DIRECTORY);
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        while (file) {
            const char* name = file.name();
            // Match pattern "*segment_XXXXX.csv"
            const char* segPtr = strstr(name, "segment_");
            if (segPtr) {
                unsigned long num = 0;
                if (sscanf(segPtr, "segment_%05lu", &num) == 1 || sscanf(segPtr, "segment_%lu", &num) == 1) {
                    if (num > highest) highest = num;
                }
            }
            file = dir.openNextFile();
        }
        dir.close();
    }
    return highest + 1;
}

/**
 * @brief Open a new trial CSV file, write self-describing metadata headers,
 * and initialize state variables.
 */
bool openTrial() {
    if (!sdReady) {
        Serial.println("[SD] Cannot start segment: SD card is unavailable");
        return false;
    }

    // Ensure any previously open trial is cleanly closed first
    closeTrial();

    trialNumber = nextSegmentNumber();
    const char* label = ownerLabel ? "owner" : "nonowner";
    char fileName[96];
    snprintf(fileName, sizeof(fileName), "%s/%s_segment_%05lu.csv",
             SD_LOG_DIRECTORY, label, static_cast<unsigned long>(trialNumber));
    trialPath = String(fileName);

    trialFile = SD_MMC.open(trialPath, FILE_WRITE);
    if (!trialFile) {
        Serial.printf("[SD] Failed to create trial file: %s\n", trialPath.c_str());
        return false;
    }

    // Write self-describing metadata header block
    trialFile.printf("schema_version=1\n");
    trialFile.printf("firmware_version=%s\n", MINAS_FW_VERSION);
    trialFile.printf("label=%s\n", ownerLabel ? "owner" : "nonowner");
    trialFile.printf("is_owner=%d\n", ownerLabel ? 1 : 0);
    trialFile.printf("sample_interval_ms=%lu\n", SAMPLE_INTERVAL_MS);
    trialFile.println("---");

    // CSV column names (exactly 22 columns)
    trialFile.println(
        "segment_number,sample_sequence,timestamp_ms,elapsed_ms,label,is_owner,"
        "controller_connected,raw_lx,raw_ly,raw_rx,raw_ry,l2,r2,buttons_mask,"
        "steering_deg,throttle_percent,steering_command_deg,esc_command_us,"
        "steering_delta,throttle_delta,sonar_distance_cm,sonar_valid"
    );
    trialFile.flush();

    // Reset trial counters and delta tracking
    sampleSequence = 0;
    samplesSinceFlush = 0;
    trialStartMs = millis();
    previousSteering = STEERING_CENTER_DEG; // Reset delta baselines to avoid leakage from prior driver
    previousThrottle = 0.0f;
    trialActive = true;

    Serial.printf("[TRIAL] Started %s (%s)\n", trialPath.c_str(), ownerLabel ? "owner" : "nonowner");
    return true;
}

/**
 * @brief Pack physical PS5 buttons into an unsigned 16-bit integer bitmask.
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
 * @brief Write one sample row to the open trial CSV file on the MicroSD card.
 * All 22 format specifiers strictly match their respective argument types.
 */
void writeSample(uint32_t now, bool connected, int rawLx, int rawLy, int rawRx, int rawRy,
                 int l2, int r2, uint16_t buttons, float steering,
                 float throttle, int steeringCommand, int escCommand,
                 float steeringDelta, float throttleDelta) {
    if (!trialFile) return;

    const int sonarValid = (sonarDistanceCm >= 0) ? 1 : 0;

    // Fixed CSV row formatting: 22 format specifiers matching exactly 22 parameters
    const size_t written = trialFile.printf(
        "%lu,%lu,%lu,%lu,%s,%d,%d,%d,%d,%d,%d,%d,%d,%u,%.2f,%.2f,%d,%d,%.4f,%.4f,%d,%d\n",
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
        throttleDelta,
        sonarDistanceCm,
        sonarValid
    );

    if (written == 0) {
        Serial.println("[SD] Write failure; closing trial and engaging neutral failsafe");
        closeTrial();
        sdReady = false;
        return;
    }

    // Flush periodically to protect data integrity without excessive write latency
    if (++samplesSinceFlush >= SD_FLUSH_EVERY_N_SAMPLES) {
        trialFile.flush();
        samplesSinceFlush = 0;
    }
}

// ============================================================================
// PERIODIC CONTROLLER SAMPLING & CONTROL LOOP
// ============================================================================
/**
 * @brief Sample ultrasonic distance, process PS5 inputs, update actuators,
 * and log data to MicroSD.
 */
void sampleController() {
    const uint32_t now = millis();

    // 1. Ultrasonic Sonar Sampling (every SONAR_SAMPLE_INTERVAL_MS, default 100 ms / 10 Hz)
    if (now - lastSonarMs >= SONAR_SAMPLE_INTERVAL_MS) {
        lastSonarMs = now;
        sonarDistanceCm = readSonarCm();
    }

    const bool connected = ps5.isConnected();
    const bool circle = connected ? ps5.Circle() : false;
    const bool cross  = connected ? ps5.Cross()  : false;

    // 2. Button Edge Detection: Circle starts or switches driver segment
    if (circle && !previousCircle) {
        if (trialActive) {
            closeTrial();
            ownerLabel = !ownerLabel; // Switch driver label
        }
        if (openTrial()) {
            // Audio confirmation tone: 2200 Hz (high) for owner, 1200 Hz (low) for nonowner
            tone(BUZZER_PIN, ownerLabel ? 2200 : 1200, ownerLabel ? 120 : 240);
            Serial.printf("[SEGMENT] Active label: %s\n", ownerLabel ? "owner" : "nonowner");
        }
    }

    // 3. Button Edge Detection: Cross closes current segment
    if (cross && !previousCross) {
        closeTrial();
        noTone(BUZZER_PIN);
    }

    previousCircle = circle;
    previousCross = cross;

    // 4. Handle Disconnect or Inactive State
    if (!trialActive) {
        applyFailsafe();
        return;
    }

    // If trial is active but controller disconnected, log a failsafe row (controller_connected = 0)
    // so data continuity is maintained and offline ML filters can identify dropouts.
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
    const int rawLx = ps5.LStickX(); // Steering: Left Stick X (-128 to 127)
    const int rawLy = ps5.LStickY();
    const int rawRx = ps5.RStickX();
    const int rawRy = ps5.RStickY();
    const int l2 = ps5.L2Value();    // Brake/Reverse pressure (0 to 255)
    const int r2 = ps5.R2Value();    // Throttle pressure (0 to 255)

    // Calculate floating-point steering angle (0° to 180°, centered at 90°)
    const float steering = ((static_cast<float>(rawLx) + 128.0f) * (STEERING_MAX_DEG - STEERING_MIN_DEG) / 255.0f) + STEERING_MIN_DEG;

    // Calculate signed throttle percentage (-100% to +100%)
    const float throttle = (static_cast<float>(r2) - static_cast<float>(l2)) * 100.0f / 255.0f;

    // Calculate temporal derivatives (rate of control change)
    const float steeringDelta = steering - previousSteering;
    const float throttleDelta = throttle - previousThrottle;

    // Compute actuator commands
    const int steeringCommand = constrain(static_cast<int>(roundf(steering)), STEERING_MIN_DEG, STEERING_MAX_DEG);
    const int escCommand = throttleToPulse(throttle);

    // 6. Actuator Output Commands
    steeringServo.write(steeringCommand);
    if (ENABLE_MOTOR_OUTPUT) {
        esc.writeMicroseconds(escCommand);
    } else {
        esc.writeMicroseconds(ESC_FAILSAFE_US); // Safety neutral during bench testing
    }

    // 7. MicroSD Data Logging
    // Note: escCommand is logged to record driver intent even when ENABLE_MOTOR_OUTPUT is false
    writeSample(now, true, rawLx, rawLy, rawRx, rawRy, l2, r2, buttonsMask(),
                steering, throttle, steeringCommand, escCommand,
                steeringDelta, throttleDelta);

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
    Serial.println("\n[INIT] Minas WROVER Driver Data Collector starting...");

    // Configure GPIOs
    pinMode(SONAR_TRIG_PIN, OUTPUT);
    digitalWrite(SONAR_TRIG_PIN, LOW);
    pinMode(SONAR_ECHO_PIN, INPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // Attach Servos and enforce neutral failsafe immediately
    steeringServo.setPeriodHertz(50);
    steeringServo.attach(STEERING_SERVO_PIN, 500, 2500);
    esc.setPeriodHertz(50);
    esc.attach(ESC_PIN, ESC_MIN_US, ESC_MAX_US);
    applyFailsafe();

    // Initialize MicroSD Card in 1-bit SDMMC Mode (fast and reliable on ESP32-WROVER)
    if (!SD_MMC.begin(SD_MOUNT_POINT, true, false)) {
        Serial.println("[SD] ERROR: SD card initialization failed. Check card insertion and format (FAT32).");
        sdReady = false;
    } else {
        if (!SD_MMC.exists(SD_LOG_DIRECTORY)) {
            if (SD_MMC.mkdir(SD_LOG_DIRECTORY)) {
                Serial.printf("[SD] Created logging directory: %s\n", SD_LOG_DIRECTORY);
                sdReady = true;
            } else {
                Serial.printf("[SD] ERROR: Failed to create logging directory: %s\n", SD_LOG_DIRECTORY);
                sdReady = false;
            }
        } else {
            sdReady = true;
            Serial.printf("[SD] Ready. Log directory: %s\n", SD_LOG_DIRECTORY);
        }
    }

    // Initialize PS5 Bluetooth Classic Interface
    if (!ps5.begin(PS5_CONTROLLER_MAC)) {
        Serial.println("[PS5] ERROR: Bluetooth host init failed. Verify controller MAC in Config.h.");
    } else {
        Serial.printf("[PS5] Bluetooth initialized for MAC: %s. Pair DualSense controller.\n", PS5_CONTROLLER_MAC);
    }

    Serial.println("[READY] Firmware initialized:");
    Serial.println("  - Circle Button: Start / Switch Driver Segment (Owner <-> Nonowner)");
    Serial.println("  - Cross Button:  Close and save active segment");
    if (!ENABLE_MOTOR_OUTPUT) {
        Serial.println("  - [SAFETY] ENABLE_MOTOR_OUTPUT is FALSE. Motor will stay neutral (Bench Mode).");
    }
}

// ============================================================================
// ARDUINO MAIN LOOP
// ============================================================================
void loop() {
    const uint32_t now = millis();

    // Enforce 20 Hz (50 ms) fixed sampling period without timer phase drift
    if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
        lastSampleMs += SAMPLE_INTERVAL_MS;
        if (now - lastSampleMs > SAMPLE_INTERVAL_MS) {
            // If execution fell significantly behind, resynchronize to current time
            lastSampleMs = now;
        }
        sampleController();
    }

    // Ensure motor stops instantly if controller disconnects
    if (!ps5.isConnected()) {
        applyFailsafe();
    }

    delay(1); // Yield to background FreeRTOS tasks (Bluetooth stack & WiFi)
}
