#include <map>
#include <string>
#include <vector>

// Mirror of satellites.h for the Python satellite test tools.
std::vector<std::map<std::string, std::string>> satellites = {
    {"Satellite", "GOES19"}, {"Sector", "FD"}, {"Product", "DayNightCloudMicroCombo"},
    {"Satellite", "GOES19"}, {"Sector", "FD"}, {"Product", "FireTemperature"},
    {"Satellite", "GOES19"}, {"Sector", "FD"}, {"Product", "GEOCOLOR"},
    {"Satellite", "GOES19"}, {"Sector", "NSA"}, {"Product", "GEOCOLOR"},
    {"Satellite", "GOES19"}, {"Sector", "FD"}, {"Product", "13"},
    {"Satellite", "GOES18"}, {"Sector", "CAK"}, {"Product", "GEOCOLOR"},
    {"Satellite", "Himawari"}, {"Sector", "REGION"}, {"Product", "Himawari_AHI_Band13_Clean_Infrared"},
    {"Satellite", "EUMETSAT"}, {"Sector", "MTG_FD"}, {"Product", "rgb_dust"},
    {"Satellite", "EUMETSAT"}, {"Sector", "MSG_FES"}, {"Product", "rgb_airmass"},
    {"Satellite", "EUMETSAT"}, {"Sector", "MTG_FD"}, {"Product", "rgb_truecolour"},
};
