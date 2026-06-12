#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>

struct SatelliteConfig {
    std::string satellite; // "GOES16", "GOES18", "GOES19"
    std::string sector;    // "FD", "AK", etc.
    std::string product;   // "GEOCOLOR", "AirMass", "01", etc.
};

enum class SatelliteMode { FIXED, RANDOM, SEQUENTIAL };

struct Config {
    bool hourly  = false;
    bool daily   = false;
    bool weekly  = false;
    bool monthly = false;
    int  pullIntervalMinutes = 10;
    SatelliteMode satelliteMode = SatelliteMode::RANDOM;
    SatelliteConfig fixedSatellite;
    bool deleteAfterTimelapse = false;
    bool useOldImages = false;
    std::string dataPath = ".";
    std::string format = "MP4";
};

Config readConfig(const std::string& path);
int    readSatelliteIndex(const std::string& path);
void   writeSatelliteIndex(const std::string& path, int index);

#endif
