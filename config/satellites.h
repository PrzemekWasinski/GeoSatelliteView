#pragma once
#include "../include/configReader.h"
#include <vector>

// Curated production list from satellite_test/keep.csv.
inline std::vector<SatelliteConfig> SATELLITE_LIST = {
    {"GOES19",    "FD",      "DayNightCloudMicroCombo"},
    {"GOES19",    "FD",      "FireTemperature"},
    {"GOES19",    "FD",      "GEOCOLOR"},
    {"GOES19",    "NSA",     "GEOCOLOR"},
    {"GOES19",    "FD",      "13"},
    {"GOES18",    "CAK",     "GEOCOLOR"},
    {"Himawari",  "REGION",  "Himawari_AHI_Band13_Clean_Infrared"},
    {"EUMETSAT",  "MTG_FD",  "rgb_dust"},
    {"EUMETSAT",  "MSG_FES", "rgb_airmass"},
    {"EUMETSAT",  "MTG_FD",  "rgb_truecolour"},
};
