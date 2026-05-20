#ifndef CONFIG_READER_H
#define CONFIG_READER_H

#include <string>

struct SatelliteConfig {
    std::string satellite; //"GOES16" or "GOES18"
    std::string sector; //"FD", "AK", etc.
    std::string product; //"GEOCOLOR", "AirMass", "01", etc.
};

struct Config {
    bool hourly = false;
    bool daily = false;
    bool weekly = false;
    bool monthly = false;
    int  pullIntervalMinutes = 10;
    bool randomSatellite = true;
    SatelliteConfig fixedSatellite;
    bool deleteAfterTimelapse = false;
};

Config readConfig(const std::string& path);

#endif
