#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <filesystem>
#include <thread>
#include <sys/statvfs.h>
#include <random>
#include <vector>

#include "../include/timelapse.h"
#include "../include/fileFunctions.h"
#include "../include/configReader.h"
#include "../include/logger.h"
#include "../config/satellites.h"

static SatelliteConfig pickRandom(const std::vector<SatelliteConfig>& list) {
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, list.size() - 1);
    return list[dist(rng)];
}

static std::string longestEnabled(const Config& cfg) {
    if (cfg.monthly) return "monthly";
    if (cfg.weekly)  return "weekly";
    if (cfg.daily)   return "daily";
    return "hourly";
}

size_t write_to_buffer(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* buf = static_cast<std::vector<char>*>(userdata);
    buf->insert(buf->end(), static_cast<char*>(ptr), static_cast<char*>(ptr) + size * nmemb);
    return size * nmemb;
}

bool checkDiskSpace(const char* path = ".") {
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
        logError("Failed to read disk stats");
        return false;
    }
    unsigned long long available = (unsigned long long)stat.f_bavail * (unsigned long long)stat.f_frsize;
    unsigned long long availableGB = available / (1024ULL * 1024ULL * 1024ULL);
    logInfo("Disk space available: " + std::to_string(availableGB) + " GB");
    return availableGB >= 10;
}

void compilePeriod(const std::filesystem::path& imageryDir, const std::filesystem::path& outputDir, bool deleteImages) {
    std::string outputFile = std::string(outputDir) + "/output.mkv";
    logInfo("Compiling timelapse: " + std::string(imageryDir));
    std::filesystem::create_directories(outputDir);
    makeTimelapse(std::string(imageryDir), outputFile, 24);
    logInfo("Timelapse done: " + outputFile);
    if (deleteImages) {
        std::filesystem::remove_all(imageryDir);
        logInfo("Deleted imagery: " + std::string(imageryDir));
    }
}

int main() {
    initLogger("./geosatelliteview.log");
    std::time_t startTime = std::time(nullptr);
    char startBuf[32];
    strftime(startBuf, sizeof(startBuf), "%d-%m-%Y %H:%M:%S", std::localtime(&startTime));
    logInfo("----- GeoSatelliteView Started at: " + std::string(startBuf) + " -----");

    Config cfg = readConfig("./config/config.yml");
    const std::string indexFile = "./config/satellite_index.txt";

    if (!cfg.hourly && !cfg.daily && !cfg.weekly && !cfg.monthly) {
        logError("No timelapse interval enabled in config.yml - exiting");
        return 1;
    }

    if (cfg.satelliteMode != SatelliteMode::FIXED && SATELLITE_LIST.empty()) {
        logError("Satellite list is empty - run tests/parse_log.py first");
        return 1;
    }

    // Log active config
    logInfo("Intervals: "
        + std::string(cfg.hourly  ? "hourly "  : "")
        + std::string(cfg.daily   ? "daily "   : "")
        + std::string(cfg.weekly  ? "weekly "  : "")
        + std::string(cfg.monthly ? "monthly"  : ""));
    logInfo("Pull interval: " + std::to_string(cfg.pullIntervalMinutes) + " min");
    logInfo("Delete after timelapse: " + std::string(cfg.deleteAfterTimelapse ? "yes" : "no"));

    // Initialise satellite
    int satIndex = 0;
    SatelliteConfig currentSat;

    switch (cfg.satelliteMode) {
        case SatelliteMode::RANDOM:
            currentSat = pickRandom(SATELLITE_LIST);
            logInfo("Mode: Random - starting with "
                + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
            break;
        case SatelliteMode::SEQUENTIAL:
            satIndex   = readSatelliteIndex(indexFile) % static_cast<int>(SATELLITE_LIST.size());
            currentSat = SATELLITE_LIST[satIndex];
            logInfo("Mode: Sequential - resuming at index " + std::to_string(satIndex)
                + " (" + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product + ")");
            break;
        case SatelliteMode::FIXED:
            currentSat = cfg.fixedSatellite;
            logInfo("Mode: Fixed - " + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
            break;
    }

    const std::string longest = longestEnabled(cfg);
    std::filesystem::path dataDir = "./data/";
    //std::filesystem::path dataDir = "/mnt/ssd/GeoSatelliteView/data/";

    std::vector<std::string> activeIntervals;
    if (cfg.hourly)  activeIntervals.push_back("hourly");
    if (cfg.daily)   activeIntervals.push_back("daily");
    if (cfg.weekly)  activeIntervals.push_back("weekly");
    if (cfg.monthly) activeIntervals.push_back("monthly");

    std::time_t initTime = std::time(nullptr);
    std::tm initTm = *std::localtime(&initTime);
    int storedHour  = initTm.tm_hour;
    int storedDay   = initTm.tm_yday + initTm.tm_year * 366;
    int storedWeek  = initTm.tm_yday / 7 + initTm.tm_year * 53;
    int storedMonth = initTm.tm_mon  + initTm.tm_year * 12;

    bool firstRun = true;
    auto lastImageFetch = std::chrono::steady_clock::now();

    while (true) {
        if (!checkDiskSpace(".")) {
            logError("Less than 10 GB remaining - stopping");
            break;
        }

        time_t now = time(nullptr);
        struct tm dt = *localtime(&now);
        char dateStr[20], timeStr[20], dateTimeStr[40];
        strftime(dateStr,     sizeof(dateStr),     "%d-%m-%Y", &dt);
        strftime(timeStr,     sizeof(timeStr),     "%H-%M-%S", &dt);
        snprintf(dateTimeStr, sizeof(dateTimeStr), "%s_%s", timeStr, dateStr);

        auto handleRollover = [&](const std::string& intervalName, std::time_t prevOffset) {
            std::string thisSat   = currentSat.satellite;
            std::string thisCombo = currentSat.satellite + "-" + currentSat.sector + "-" + currentSat.product;

            std::time_t prevTime = now - prevOffset;
            std::tm prevTm = *std::localtime(&prevTime);
            char prevDateBuf[20];
            strftime(prevDateBuf, sizeof(prevDateBuf), "%d-%m-%Y", &prevTm);

            logInfo(intervalName + " period ended for " + thisCombo + " (" + prevDateBuf + ")");

            std::filesystem::path prevImagery = dataDir / thisSat / intervalName / thisCombo / prevDateBuf / "imagery";
            std::filesystem::path prevOutput  = dataDir / thisSat / intervalName / thisCombo / prevDateBuf / "output";

            if (pathExists(prevImagery)) {
                bool del = cfg.deleteAfterTimelapse;
                std::thread([prevImagery, prevOutput, del]() {
                    compilePeriod(prevImagery, prevOutput, del);
                }).detach();
            } else {
                logWarn("No imagery found for " + thisCombo + " " + std::string(prevDateBuf) + " [" + intervalName + "] - skipping timelapse");
            }

            if (intervalName == longest) {
                if (cfg.satelliteMode == SatelliteMode::RANDOM) {
                    currentSat = pickRandom(SATELLITE_LIST);
                    logInfo("Random switch -> "
                        + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
                } else if (cfg.satelliteMode == SatelliteMode::SEQUENTIAL) {
                    satIndex   = (satIndex + 1) % static_cast<int>(SATELLITE_LIST.size());
                    currentSat = SATELLITE_LIST[satIndex];
                    writeSatelliteIndex(indexFile, satIndex);
                    logInfo("Sequential switch [" + std::to_string(satIndex) + "] -> "
                        + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
                }
            }
        };

        int curHour  = dt.tm_hour;
        int curDay   = dt.tm_yday + dt.tm_year * 366;
        int curWeek  = dt.tm_yday / 7 + dt.tm_year * 53;
        int curMonth = dt.tm_mon  + dt.tm_year * 12;

        if (cfg.hourly  && curHour  != storedHour)  { handleRollover("hourly",  3600);     storedHour  = curHour;  }
        if (cfg.daily   && curDay   != storedDay)   { handleRollover("daily",   86400);    storedDay   = curDay;   }
        if (cfg.weekly  && curWeek  != storedWeek)  { handleRollover("weekly",  7*86400);  storedWeek  = curWeek;  }
        if (cfg.monthly && curMonth != storedMonth) { handleRollover("monthly", 30*86400); storedMonth = curMonth; }

        std::string satNum    = currentSat.satellite.substr(4);
        std::string comboName = currentSat.satellite + "-" + currentSat.sector + "-" + currentSat.product;
        std::string imageUrl  = "https://cdn.star.nesdis.noaa.gov/GOES" + satNum
                              + "/ABI/" + (currentSat.sector == "FD" ? "FD" : "SECTOR/" + currentSat.sector)
                              + "/" + currentSat.product + "/latest.jpg";

        for (const auto& interval : activeIntervals)
            createRunDirectories(dataDir, currentSat.satellite, interval, comboName, dateStr);

        auto currentTimestamp = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(currentTimestamp - lastImageFetch);

        if (elapsed.count() >= cfg.pullIntervalMinutes || firstRun) {
            CURL* curl = curl_easy_init();
            if (!curl) {
                logError("Failed to initialise curl");
                return 1;
            }

            std::vector<char> imgBuffer;
            curl_easy_setopt(curl, CURLOPT_URL, imageUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &imgBuffer);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK) {
                logError("Curl failed for " + comboName + ": " + curl_easy_strerror(res));
            } else {
                std::string filename = std::string(dateTimeStr) + ".jpg";
                for (const auto& interval : activeIntervals) {
                    std::filesystem::path imagePath = dataDir / currentSat.satellite / interval
                                                    / comboName / dateStr / "imagery" / filename;
                    std::ofstream file(imagePath, std::ios::binary);
                    file.write(imgBuffer.data(), imgBuffer.size());
                }
                logInfo("Saved " + comboName + " " + dateTimeStr);
            }

            lastImageFetch = currentTimestamp;
            firstRun = false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    logInfo("=== GeoSatelliteView stopped ===");
    return 0;
}
