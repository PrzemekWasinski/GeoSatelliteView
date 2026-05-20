#include "../include/configReader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <vector>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool parseBool(const std::string& val) {
    std::string v = val;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return v == "true";
}

Config readConfig(const std::string& path) {
    Config cfg;
    std::ifstream file(path);
    std::string line;

    while (std::getline(file, line)) {
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        if (val.empty()) continue;

        if      (key == "Hourly")   cfg.hourly   = parseBool(val);
        else if (key == "Daily")    cfg.daily    = parseBool(val);
        else if (key == "Weekly")   cfg.weekly   = parseBool(val);
        else if (key == "Monthly")  cfg.monthly  = parseBool(val);
        else if (key == "Interval") cfg.pullIntervalMinutes = std::stoi(val);
        else if (key == "Delete")   cfg.deleteAfterTimelapse = parseBool(val);
        else if (key == "Satellite") {
            if (val == "None") {
                cfg.randomSatellite = true;
            } else {
                cfg.randomSatellite = false;
                // Parse [GOES18, PNW, AirMass]
                val.erase(std::remove(val.begin(), val.end(), '['), val.end());
                val.erase(std::remove(val.begin(), val.end(), ']'), val.end());
                std::istringstream ss(val);
                std::string token;
                std::vector<std::string> parts;
                while (std::getline(ss, token, ','))
                    parts.push_back(trim(token));
                if (parts.size() >= 3)
                    cfg.fixedSatellite = {parts[0], parts[1], parts[2]};
            }
        }
    }
    return cfg;
}
