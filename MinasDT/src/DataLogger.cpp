#include "DataLogger.h"
#include "Config.h"
#include <math.h>

#define SD_MMC_CMD 15
#define SD_MMC_CLK 14
#define SD_MMC_D0 2

DataLogger::DataLogger() : _initialized(false), _isOwnerMode(true), _lastLogTime(0) {
    _steerSum = _throttleSum = _steerSqSum = _throttleSqSum = 0;
    _lastSteer = _lastThrottle = 90; // Neutral start
    _jerkSum = 0;
    _sampleCount = 0;
    _disconnectCount = _connectCount = 0;
}

bool DataLogger::begin() {
    SD_MMC.setPins(SD_MMC_CLK, SD_MMC_CMD, SD_MMC_D0);
    if (!SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT, 5)) {
        return false;
    }
    _initialized = true;
    createNewLogFile();
    return true;
}

void DataLogger::setOwnerMode(bool isOwner) {
    _isOwnerMode = isOwner;
}

void DataLogger::createNewLogFile() {
    if (!_initialized) return;
    _currentFileName = "/log_" + String(millis() / 1000) + ".csv";
    File file = SD_MMC.open(_currentFileName, FILE_WRITE);
    if (file) {
        // CSV schema contains only current telemetry and disconnect count.
        file.println("Timestamp,IsOwner,AvgSteer,StdDevSteer,AvgThrottle,StdDevThrottle,EstSpeed,AvgJerk,Disconnects");
        file.close();
    }
}

void DataLogger::sample(int steer, int throttle) {
    _steerSum += steer;
    _throttleSum += throttle;
    _steerSqSum += (float)(steer * steer);
    _throttleSqSum += (float)(throttle * throttle);
    
    // Jerk calculation (rate of change of input)
    _jerkSum += abs(steer - _lastSteer) + abs(throttle - _lastThrottle);
    
    _lastSteer = steer;
    _lastThrottle = throttle;
    _sampleCount++;
}

void DataLogger::logDisconnect() { _disconnectCount++; }
void DataLogger::logConnect() { _connectCount++; }

void DataLogger::update(unsigned long intervalMs) {
    if (!_initialized || _sampleCount == 0) return;
    unsigned long now = millis();
    if (now - _lastLogTime >= intervalMs) {
        _lastLogTime = now;
        
        float avgSteer = _steerSum / _sampleCount;
        float avgThrottle = _throttleSum / _sampleCount;
        float steerVar = (_steerSqSum / _sampleCount) - (avgSteer * avgSteer);
        float throttleVar = (_throttleSqSum / _sampleCount) - (avgThrottle * avgThrottle);
        float avgJerk = _jerkSum / _sampleCount;
        
        // Speed estimation: based on throttle intensity and friction
        float throttleIntensity = abs(avgThrottle - 90) / 90.0;
        float estimatedSpeed = throttleIntensity * 100.0 * FRICTION_COEFFICIENT;

        File file = SD_MMC.open(_currentFileName, FILE_APPEND);
        if (file) {
            file.printf("%lu,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n",
                now, _isOwnerMode ? 1 : 0, avgSteer, sqrt(max(0.0f, steerVar)), 
                avgThrottle, sqrt(max(0.0f, throttleVar)), estimatedSpeed, avgJerk,
                _disconnectCount);
            file.close();
        }

        // Reset accumulators
        _steerSum = _throttleSum = _steerSqSum = _throttleSqSum = _jerkSum = 0;
        _sampleCount = 0;
    }
}
