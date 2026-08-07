#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

class DataLogger {
private:
    bool _initialized;
    unsigned long _lastLogTime;
    String _currentFileName;

    float _steerSum, _throttleSum, _steerSqSum, _throttleSqSum;
    int _sampleCount, _rewindCount, _disconnectCount, _connectCount;

    void createNewLogFile();

public:
    DataLogger(); // No CS pin needed for the built-in SDMMC slot
    bool begin();
    void sample(int steer, int throttle);
    void logRewind();
    void logDisconnect();
    void logConnect();
    void update(unsigned long intervalMs);
};

#endif