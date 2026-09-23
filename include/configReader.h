#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>

struct SatelliteConfig {
    std::string satellite; // GOES18, GOES19, Himawari, or EUMETSAT
    std::string sector;    // FD, CAK, REGION, MTG_FD, etc.
    std::string product;   // Provider product or band identifier
};

enum class SatelliteMode { FIXED, RANDOM, SEQUENTIAL };

struct Config {
    bool hourly  = false;
    bool daily   = false;
    bool weekly  = false;
    bool monthly = false;
    int  pullIntervalMinutes = 10;
    int  encoderWorkers = 3;
    int  minimumDiskGB = 10;
    SatelliteMode satelliteMode = SatelliteMode::RANDOM;
    SatelliteConfig fixedSatellite;
    bool deleteAfterTimelapse = false;
    bool useOldImages = false;
    double keepInterval = 0.0;
    std::string dataPath = ".";
    std::string format = "MP4";
};

Config readConfig(const std::string& path);
int    readSatelliteIndex(const std::string& path);
void   writeSatelliteIndex(const std::string& path, int index);

#endif
