#include "DataLogger.h"

// Freenove ESP32-Wrover built-in SD slot fixed pins
#define SD_MMC_CMD  15
#define SD_MMC_CLK  14
#define SD_MMC_D0    2

DataLogger::DataLogger() : _initialized(false), _lastLogTime(0) {
    _steerSum = _throttleSum = _steerSqSum = _throttleSqSum = 0;
    _sampleCount = _rewindCount = _disconnectCount = _connectCount = 0;
}

bool DataLogger::begin() {
    Serial.println("--- SD Card Diagnostic (Built-in SD_MMC) ---");
    
    // Explicitly map the pins for the Wrover board
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);

    // Initialize in 1-bit mode with the /sdcard mount point
    if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
        Serial.println("ERROR: SD_MMC.begin() FAILED!");
        Serial.println("Check: 1. Is the card inserted? 2. Is it FAT16/FAT32? 3. Is the slot clean?");
        return false;
    }
    
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("ERROR: No SD card detected in slot.");
        return false;
    }

    Serial.print("SUCCESS: SD Card Detected. Type: ");
    if (cardType == CARD_MMC) Serial.println("MMC");
    else if (cardType == CARD_SD) Serial.println("SDSC");
    else if (cardType == CARD_SDHC) Serial.println("SDHC");
    else Serial.println("UNKNOWN");

    _initialized = true;
    createNewLogFile();
    return true;
}

void DataLogger::createNewLogFile() {
    if (!_initialized) return;
    _currentFileName = "/log_" + String(millis() / 1000) + ".csv";
    Serial.println("Creating new log file: " + _currentFileName);

    File file = SD_MMC.open(_currentFileName, FILE_WRITE); 
    if (file) {
        file.println("Timestamp,AvgSteer,StdDevSteer,AvgThrottle,StdDevThrottle,Rewinds,Disconnects,Connects");
        file.close();
        Serial.println("CSV Header written successfully.");
    } else {
        Serial.println("ERROR: Failed to create file!");
    }
}

void DataLogger::sample(int steer, int throttle) {
    _steerSum += steer; _throttleSum += throttle;
    _steerSqSum += (float)(steer * steer); _throttleSqSum += (float)(throttle * throttle);
    _sampleCount++;
}

void DataLogger::logRewind() { _rewindCount++; }
void DataLogger::logDisconnect() { _disconnectCount++; }
void DataLogger::logConnect() { _connectCount++; }

void DataLogger::update(unsigned long intervalMs) {
    if (!_initialized) return;
    unsigned long now = millis();
    if (now - _lastLogTime >= intervalMs) {
        _lastLogTime = now;
        if (_sampleCount == 0) return;

        float avgSteer = _steerSum / _sampleCount;
        float avgThrottle = _throttleSum / _sampleCount;
        float steerVar = (_steerSqSum / _sampleCount) - (avgSteer * avgSteer);
        float throttleVar = (_throttleSqSum / _sampleCount) - (avgThrottle * avgThrottle);

        File file = SD_MMC.open(_currentFileName, FILE_APPEND);
        if (file) {
            file.printf("%lu,%.2f,%.2f,%.2f,%.2f,%d,%d,%d\n", 
                        now, avgSteer, sqrt(max(0.0f, steerVar)), 
                        avgThrottle, sqrt(max(0.0f, throttleVar)), 
                        _rewindCount, _disconnectCount, _connectCount);
            file.close();
        } else {
            Serial.println("LOG ERROR: Could not open file for appending!");
        }

        _steerSum = _throttleSum = _steerSqSum = _throttleSqSum = 0;
        _sampleCount = _rewindCount = _disconnectCount = _connectCount = 0;
    }
}