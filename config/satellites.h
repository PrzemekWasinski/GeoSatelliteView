#pragma once
#include "../include/configReader.h"
#include <vector>

// Curated production list from satellite_test/keep.csv.
inline std::vector<SatelliteConfig> SATELLITE_LIST = {
    {"GOES18", "CAK", "GEOCOLOR"},
    {"GOES18", "FD", "GEOCOLOR"},
    {"GOES18", "FD", "DayNightCloudMicroCombo"},
    {"GOES18", "FD", "13"},
    {"GOES18", "FD", "FireTemperature"},
    {"GOES18", "FD", "Dust"},
    {"GOES18", "M1", "GEOCOLOR"},
    {"GOES18", "M2", "GEOCOLOR"},
    {"GOES19", "FD", "GEOCOLOR"},
    {"GOES19", "FD", "DayNightCloudMicroCombo"},
    {"GOES19", "FD", "13"},
    {"GOES19", "FD", "FireTemperature"},
    {"GOES19", "FD", "Dust"},
    {"GOES19", "MESO1", "GEOCOLOR"},
    {"GOES19", "PNW", "10"},
    {"GOES19", "TAW", "GEOCOLOR"},
    {"EUMETSAT", "MTG_FD", "rgb_dust"},
    {"EUMETSAT", "MSG_FES", "rgb_airmass"},
    {"EUMETSAT", "MTG_FD", "rgb_truecolour"},
};
