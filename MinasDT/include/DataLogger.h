#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>

class DataLogger {
private:
    bool _initialized;
    bool _isOwnerMode;
    unsigned long _lastLogTime;
    String _currentFileName;

    // Feature accumulation
    float _steerSum, _throttleSum, _steerSqSum, _throttleSqSum;
    float _lastSteer, _lastThrottle;
    float _jerkSum;
    int _sampleCount;
    
    // System events
    int _rewindCount, _disconnectCount, _connectCount;

    void createNewLogFile();

public:
    DataLogger();
    bool begin();
    void setOwnerMode(bool isOwner);
    bool isOwnerMode() const { return _isOwnerMode; }
    
    void sample(int steer, int throttle);
    void logRewind();
    void logDisconnect();
    void logConnect();
    void update(unsigned long intervalMs);
};

#endif
