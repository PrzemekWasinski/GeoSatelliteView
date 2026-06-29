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
#include <map>
#include <set>
#include <cstdio>
#include <algorithm>

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

static std::time_t parseImageTime(const std::string& filename) {
    std::tm tm{};
    if (std::sscanf(filename.c_str(), "%4d%2d%2d_%2d%2d%2d",
                    &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
        return -1;
    tm.tm_year -= 1900;
    tm.tm_mon  -= 1;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

static std::string longestEnabled(const Config& cfg) {
    if (cfg.monthly) return "monthly";
    if (cfg.weekly)  return "weekly";
    if (cfg.daily)   return "daily";
    return "hourly";
}

// Scans data/{satellite}/{interval}/{combo}/{period}/ and returns the SATELLITE_LIST
// index of the combo whose most recent period folder has the latest timestamp name.
// Returns -1 if the data directory is absent or no combo matches the list.
static int detectLastSatIndex(const std::filesystem::path& dataDir,
                               const std::vector<SatelliteConfig>& list) {
    if (!std::filesystem::exists(dataDir)) return -1;

    std::string bestPeriodKey;
    std::string bestCombo;

    try {
        for (const auto& satEntry : std::filesystem::directory_iterator(dataDir)) {
            if (!satEntry.is_directory()) continue;
            for (const auto& ivEntry : std::filesystem::directory_iterator(satEntry)) {
                if (!ivEntry.is_directory()) continue;
                for (const auto& comboEntry : std::filesystem::directory_iterator(ivEntry)) {
                    if (!comboEntry.is_directory()) continue;
                    for (const auto& periodEntry : std::filesystem::directory_iterator(comboEntry)) {
                        if (!periodEntry.is_directory()) continue;
                        std::string key = periodEntry.path().filename().string();
                        if (key > bestPeriodKey) {
                            bestPeriodKey = key;
                            bestCombo     = comboEntry.path().filename().string();
                        }
                    }
                }
            }
        }
    } catch (...) { return -1; }

    if (bestCombo.empty()) return -1;

    // Parse "SATELLITE-SECTOR-PRODUCT" (e.g. "GOES18-FD-GEOCOLOR")
    auto first = bestCombo.find('-');
    if (first == std::string::npos) return -1;
    auto second = bestCombo.find('-', first + 1);
    if (second == std::string::npos) return -1;

    std::string satellite = bestCombo.substr(0, first);
    std::string sector    = bestCombo.substr(first + 1, second - first - 1);
    std::string product   = bestCombo.substr(second + 1);

    for (int i = 0; i < static_cast<int>(list.size()); ++i) {
        if (list[i].satellite == satellite &&
            list[i].sector    == sector    &&
            list[i].product   == product)
            return i;
    }
    return -1;
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

bool compilePeriod(
    const std::filesystem::path& imageryDir,
    const std::filesystem::path& outputDir,
    bool deleteImages,
    const std::string& format,
    double keepIntervalHours,
    std::time_t periodStart
) {
    std::string outputFile = std::string(outputDir) + "/output" + formatExtension(format);
    logInfo("Compiling timelapse: " + std::string(imageryDir));
    std::filesystem::create_directories(outputDir);
    bool ok = makeTimelapse(std::string(imageryDir), outputFile, 24, format);
    if (!ok) {
        logError("Timelapse failed - keeping imagery: " + std::string(imageryDir));
        return false;
    }
    logInfo("Timelapse done: " + outputFile);
    if (deleteImages) {
        if (keepIntervalHours <= 0.0) {
            std::filesystem::remove_all(imageryDir);
            logInfo("Deleted imagery: " + std::string(imageryDir));
        } else {
            long keepSecs = static_cast<long>(keepIntervalHours * 3600.0 + 0.5);

            std::vector<std::filesystem::path> imageFiles;
            for (const auto& entry : std::filesystem::directory_iterator(imageryDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                if (ext == ".jpg" || ext == ".jpeg" || ext == ".png")
                    imageFiles.push_back(entry.path());
            }
            std::sort(imageFiles.begin(), imageFiles.end());

            // For each N-hour slot keep the last (most recent) image in that slot.
            std::map<long, std::filesystem::path> slotKeep;
            for (const auto& p : imageFiles) {
                std::time_t t = parseImageTime(p.filename().string());
                if (t == -1) continue;
                long offset = static_cast<long>(t - periodStart);
                if (offset < 0) offset = 0;
                slotKeep[offset / keepSecs] = p; // later images overwrite earlier ones in the same slot
            }

            std::set<std::filesystem::path> toKeep;
            for (auto& [slot, p] : slotKeep) toKeep.insert(p);

            int deleted = 0;
            for (const auto& p : imageFiles) {
                if (toKeep.find(p) == toKeep.end()) {
                    std::filesystem::remove(p);
                    ++deleted;
                }
            }
            logInfo("Selective delete: kept " + std::to_string(toKeep.size())
                    + ", deleted " + std::to_string(deleted) + " from " + std::string(imageryDir));
        }
    }
    return true;
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

    const std::string longest = longestEnabled(cfg);
    std::filesystem::path dataDir = std::filesystem::path(cfg.dataPath) / "data";

    // Initialise satellite
    int satIndex = 0;
    SatelliteConfig currentSat;

    switch (cfg.satelliteMode) {
        case SatelliteMode::RANDOM:
            currentSat = pickRandom(SATELLITE_LIST);
            logInfo("Mode: Random - starting with "
                + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
            break;
        case SatelliteMode::SEQUENTIAL: {
            if (std::ifstream(indexFile)) {
                satIndex = readSatelliteIndex(indexFile) % static_cast<int>(SATELLITE_LIST.size());
                logInfo("Mode: Sequential - resuming from saved index " + std::to_string(satIndex));
            } else {
                int detected = detectLastSatIndex(dataDir, SATELLITE_LIST);
                if (detected >= 0) {
                    satIndex = detected;
                    logInfo("Mode: Sequential - detected last combo at index " + std::to_string(satIndex));
                }
            }
            currentSat = SATELLITE_LIST[satIndex];
            logInfo("Starting with " + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
            break;
        }
        case SatelliteMode::FIXED:
            currentSat = cfg.fixedSatellite;
            logInfo("Mode: Fixed - " + currentSat.satellite + " " + currentSat.sector + " " + currentSat.product);
            break;
    }

    std::vector<std::string> activeIntervals;
    if (cfg.hourly)  activeIntervals.push_back("hourly");
    if (cfg.daily)   activeIntervals.push_back("daily");
    if (cfg.weekly)  activeIntervals.push_back("weekly");
    if (cfg.monthly) activeIntervals.push_back("monthly");

    // Period length per interval (seconds), anchored to program start - not the calendar.
    auto intervalLength = [](const std::string& iv) -> std::time_t {
        if (iv == "hourly") return 3600;
        if (iv == "daily")  return 86400;
        if (iv == "weekly") return 7 * 86400;
        return 30 * 86400; // monthly (fixed 30 days)
    };
    // ISO-ordered key so a period folder name sorts chronologically and is stable across midnight.
    auto periodKey = [](std::time_t t) {
        std::tm tm = *std::localtime(&t);
        char buf[24];
        strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
        return std::string(buf);
    };

    // Each active interval runs its own period; all start at program start unless resumed below.
    std::map<std::string, std::time_t> periodStart;
    for (const auto& iv : activeIntervals) periodStart[iv] = startTime;

    // Handle imagery left over from a previous (incomplete) run of the current combo.
    {
        std::string initCombo = currentSat.satellite + "-" + currentSat.sector + "-" + currentSat.product;
        for (const auto& iv : activeIntervals) {
            std::filesystem::path comboDir = dataDir / currentSat.satellite / iv / initCombo;
            if (!pathExists(comboDir)) continue;

            // Period folders that hold imagery but were never compiled into a video.
            std::vector<std::string> incomplete;
            for (const auto& entry : std::filesystem::directory_iterator(comboDir)) {
                if (!entry.is_directory()) continue;
                std::filesystem::path imageryDir = entry.path() / "imagery";
                std::filesystem::path outputDir = entry.path() / "output";
                bool hasImages = false;
                if (pathExists(imageryDir))
                    for (const auto& f : std::filesystem::directory_iterator(imageryDir))
                        if (f.is_regular_file()) { hasImages = true; break; }
                bool hasVideo = false;
                if (std::filesystem::exists(outputDir))
                    for (const auto& f : std::filesystem::directory_iterator(outputDir))
                        if (f.is_regular_file()) { hasVideo = true; break; }
                if (hasImages && !hasVideo)
                    incomplete.push_back(entry.path().filename().string());
            }
            if (incomplete.empty()) continue;
            std::sort(incomplete.begin(), incomplete.end());

            if (cfg.useOldImages) {
                // Resume the most recent unfinished period; keep its original schedule.
                const std::string& latest = incomplete.back();
                std::tm tm{};
                if (std::sscanf(latest.c_str(), "%4d%2d%2d_%2d%2d%2d",
                                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
                    tm.tm_year -= 1900;
                    tm.tm_mon  -= 1;
                    tm.tm_isdst = -1;
                    periodStart[iv] = std::mktime(&tm);
                    logInfo("Resuming incomplete period [" + iv + "]: " + latest);
                }
                for (size_t i = 0; i + 1 < incomplete.size(); ++i) {
                    std::filesystem::remove_all(comboDir / incomplete[i]);
                    logInfo("Cleared older incomplete period [" + iv + "]: " + incomplete[i]);
                }
            } else {
                for (const auto& key : incomplete) {
                    std::filesystem::remove_all(comboDir / key);
                    logInfo("Cleared incomplete period [" + iv + "]: " + key);
                }
            }
        }
    }

    bool firstRun = true;
    auto lastImageFetch = std::chrono::steady_clock::now();

    while (true) {
        std::time_t now = std::time(nullptr);
        std::string comboName = currentSat.satellite + "-" + currentSat.sector + "-" + currentSat.product;

        // --- Period rollovers (anchored to program start) ---
        bool longestRolled = false;
        for (const auto& iv : activeIntervals) {
            std::time_t length = intervalLength(iv);
            while (now - periodStart[iv] >= length) {
                std::string key = periodKey(periodStart[iv]);
                std::filesystem::path periodDir = dataDir / currentSat.satellite / iv / comboName / key;
                std::filesystem::path prevImagery = periodDir / "imagery";
                std::filesystem::path prevOutput  = periodDir / "output";

                logInfo(iv + " period ended for " + comboName + " (" + key + ")");

                if (pathExists(prevImagery)) {
                    bool del = cfg.deleteAfterTimelapse;
                    std::string fmt = cfg.format;
                    double ki = cfg.keepInterval;
                    std::time_t ps = periodStart[iv];
                    std::thread([prevImagery, prevOutput, del, fmt, ki, ps]() {
                        compilePeriod(prevImagery, prevOutput, del, fmt, ki, ps);
                    }).detach();
                } else {
                    logWarn("No imagery for " + comboName + " [" + iv + "] " + key + " - skipping timelapse");
                }

                periodStart[iv] += length;
                if (iv == longest) longestRolled = true;
            }
        }

        // Switch satellite only after the longest period has fully completed.
        if (longestRolled) {
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
            comboName = currentSat.satellite + "-" + currentSat.sector + "-" + currentSat.product;
        }

        // --- Build the download URL for the (possibly new) current combo ---
        // Full Disk numbered bands have no latest.jpg, so use the fixed 1808x1808 file for all FD
        // products; sectors only reliably expose latest.jpg.
        std::string satNum   = currentSat.satellite.substr(4);
        std::string imageUrl = currentSat.sector == "FD"
            ? "https://cdn.star.nesdis.noaa.gov/GOES" + satNum + "/ABI/FD/"
                  + currentSat.product + "/1808x1808.jpg"
            : "https://cdn.star.nesdis.noaa.gov/GOES" + satNum + "/ABI/SECTOR/"
                  + currentSat.sector + "/" + currentSat.product + "/latest.jpg";

        for (const auto& iv : activeIntervals)
            createRunDirectories(dataDir, currentSat.satellite, iv, comboName, periodKey(periodStart[iv]));

        auto currentTimestamp = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(currentTimestamp - lastImageFetch);

        if (elapsed.count() >= cfg.pullIntervalMinutes || firstRun) {
            if (!checkDiskSpace(dataDir.c_str())) {
                logError("Less than 10 GB remaining on data path - stopping");
                break;
            }

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
            // Without these, a stalled connection (common on Wi-Fi) blocks
            // curl_easy_perform forever and freezes the whole single-threaded loop.
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L); // max time to establish connection
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);       // max time for the whole transfer
            // Abort if transfer drops below 100 B/s for 30s (catches mid-download stalls).
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);        // timeouts must not rely on signals

            CURLcode res = curl_easy_perform(curl);
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            curl_easy_cleanup(curl);

            // Only accept a genuine JPEG (HTTP 200 + JPEG magic bytes); never store error pages.
            bool validJpeg = imgBuffer.size() > 3 &&
                             static_cast<unsigned char>(imgBuffer[0]) == 0xFF &&
                             static_cast<unsigned char>(imgBuffer[1]) == 0xD8 &&
                             static_cast<unsigned char>(imgBuffer[2]) == 0xFF;

            if (res != CURLE_OK) {
                logError("Curl failed for " + comboName + ": " + curl_easy_strerror(res));
            } else if (httpCode != 200) {
                logWarn("HTTP " + std::to_string(httpCode) + " for " + comboName
                        + " (" + imageUrl + ") - skipping");
            } else if (!validJpeg) {
                logWarn("Non-JPEG response for " + comboName + " ("
                        + std::to_string(imgBuffer.size()) + " bytes) - skipping");
            } else {
                std::string filename = periodKey(now) + ".jpg"; // capture time, chronologically sortable
                for (const auto& iv : activeIntervals) {
                    std::filesystem::path imagePath = dataDir / currentSat.satellite / iv
                                                    / comboName / periodKey(periodStart[iv]) / "imagery" / filename;
                    std::ofstream file(imagePath, std::ios::binary);
                    file.write(imgBuffer.data(), imgBuffer.size());
                }
                logInfo("Saved " + comboName + " " + filename);
            }

            lastImageFetch = currentTimestamp;
            firstRun = false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    logInfo("=== GeoSatelliteView stopped ===");
    return 0;
}
