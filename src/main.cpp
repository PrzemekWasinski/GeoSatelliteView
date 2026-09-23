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
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

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

static std::string imageUrl(const SatelliteConfig& config) {
    if (config.satellite.rfind("GOES", 0) == 0) {
        std::string satNum = config.satellite.substr(4);
        if (config.sector == "FD")
            return "https://cdn.star.nesdis.noaa.gov/GOES" + satNum + "/ABI/FD/"
                 + config.product + "/1808x1808.jpg";
        return "https://cdn.star.nesdis.noaa.gov/GOES" + satNum + "/ABI/SECTOR/"
             + config.sector + "/" + config.product + "/latest.jpg";
    }

    if (config.satellite == "Himawari") {
        return "https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi?"
               "service=WMS&version=1.3.0&request=GetMap&layers=" + config.product +
               "&styles=&crs=CRS%3A84&bbox=80%2C-60%2C180%2C60&width=1024&height=1229"
               "&format=image%2Fjpeg";
    }

    if (config.satellite == "EUMETSAT") {
        std::string layer = config.sector == "MSG_FES" ? "msg_fes" : "mtg_fd";
        return "https://view.eumetsat.int/geoserver/wms?service=WMS&version=1.3.0"
               "&request=GetMap&layers=" + layer + "%3A" + config.product +
               "&styles=&crs=CRS%3A84&bbox=-65%2C-65%2C65%2C65&width=1024&height=1024"
               "&format=image%2Fjpeg";
    }

    return {};
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

bool checkDiskSpace(const char* path = ".", int minimumGB = 10) {
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
        logError("Failed to read disk stats");
        return false;
    }
    unsigned long long available = (unsigned long long)stat.f_bavail * (unsigned long long)stat.f_frsize;
    unsigned long long availableGB = available / (1024ULL * 1024ULL * 1024ULL);
    logInfo("Disk space available: " + std::to_string(availableGB) + " GB");
    return availableGB >= static_cast<unsigned long long>(minimumGB);
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

class EncodingQueue {
public:
    explicit EncodingQueue(size_t workerCount) {
        workerCount = std::max<size_t>(1, workerCount);
        for (size_t i = 0; i < workerCount; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        ready.wait(lock, [this]() { return stopping || !tasks.empty(); });
                        if (stopping && tasks.empty()) return;
                        task = std::move(tasks.front());
                        tasks.pop_front();
                    }
                    task();
                }
            });
        }
    }

    ~EncodingQueue() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        ready.notify_all();
        for (auto& worker : workers)
            if (worker.joinable()) worker.join();
    }

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.push_back(std::move(task));
        }
        ready.notify_one();
    }

private:
    std::vector<std::thread> workers;
    std::deque<std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable ready;
    bool stopping = false;
};

struct SourceState {
    SatelliteConfig config;
    std::map<std::string, std::time_t> periodStart;
    std::chrono::steady_clock::time_point nextFetch;
};

static std::string comboName(const SatelliteConfig& config) {
    return config.satellite + "-" + config.sector + "-" + config.product;
}

static bool directoryHasImages(const std::filesystem::path& directory) {
    if (!pathExists(directory)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext == ".jpg" || ext == ".jpeg" || ext == ".png") return true;
    }
    return false;
}

int main() {
    initLogger("./geosatelliteview.log");
    const std::time_t startTime = std::time(nullptr);
    char startBuf[32];
    strftime(startBuf, sizeof(startBuf), "%d-%m-%Y %H:%M:%S", std::localtime(&startTime));
    logInfo("----- GeoSatelliteView Started at: " + std::string(startBuf) + " -----");

    Config cfg = readConfig("./config/config.yml");
    if (!cfg.hourly && !cfg.daily && !cfg.weekly && !cfg.monthly) {
        logError("No timelapse interval enabled in config.yml - exiting");
        return 1;
    }
    if (SATELLITE_LIST.empty()) {
        logError("Satellite list is empty");
        return 1;
    }
    if (cfg.pullIntervalMinutes < 1) {
        logError("Interval must be at least one minute");
        return 1;
    }

    std::vector<std::string> activeIntervals;
    if (cfg.hourly)  activeIntervals.push_back("hourly");
    if (cfg.daily)   activeIntervals.push_back("daily");
    if (cfg.weekly)  activeIntervals.push_back("weekly");
    if (cfg.monthly) activeIntervals.push_back("monthly");

    auto intervalLength = [](const std::string& interval) -> std::time_t {
        if (interval == "hourly") return 3600;
        if (interval == "daily")  return 86400;
        if (interval == "weekly") return 7 * 86400;
        return 30 * 86400;
    };
    auto periodKey = [](std::time_t value) {
        std::tm tm = *std::localtime(&value);
        char buffer[24];
        strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &tm);
        return std::string(buffer);
    };

    std::filesystem::path dataDir = std::filesystem::path(cfg.dataPath) / "data";
    std::filesystem::create_directories(dataDir);

    const auto cycle = std::chrono::seconds(cfg.pullIntervalMinutes * 60);
    const auto spacing = std::chrono::seconds(
        std::max<long long>(1, cycle.count() / static_cast<long long>(SATELLITE_LIST.size())));
    const auto scheduleStart = std::chrono::steady_clock::now();

    std::vector<SourceState> sources;
    sources.reserve(SATELLITE_LIST.size());
    for (size_t i = 0; i < SATELLITE_LIST.size(); ++i) {
        SourceState state;
        state.config = SATELLITE_LIST[i];
        state.nextFetch = scheduleStart + spacing * static_cast<long long>(i + 1);
        for (const auto& interval : activeIntervals) state.periodStart[interval] = startTime;
        sources.push_back(std::move(state));
        logInfo("Scheduled " + comboName(SATELLITE_LIST[i]) + " at +"
                + std::to_string(spacing.count() * static_cast<long long>(i + 1))
                + " seconds, then every " + std::to_string(cfg.pullIntervalMinutes) + " minutes");
    }

    logInfo("Active sources: " + std::to_string(sources.size()));
    logInfo("Encoder workers: " + std::to_string(cfg.encoderWorkers));
    logInfo("Delete after timelapse: " + std::string(cfg.deleteAfterTimelapse ? "yes" : "no"));
    EncodingQueue encoders(static_cast<size_t>(cfg.encoderWorkers));

    // Resume or clear unfinished periods independently for every source.
    for (auto& source : sources) {
        const std::string name = comboName(source.config);
        for (const auto& interval : activeIntervals) {
            std::filesystem::path comboDir = dataDir / source.config.satellite / interval / name;
            if (!pathExists(comboDir)) continue;

            std::vector<std::string> incomplete;
            for (const auto& entry : std::filesystem::directory_iterator(comboDir)) {
                if (!entry.is_directory()) continue;
                const auto imageryDir = entry.path() / "imagery";
                const auto outputDir = entry.path() / "output";
                bool hasVideo = false;
                if (pathExists(outputDir)) {
                    for (const auto& file : std::filesystem::directory_iterator(outputDir)) {
                        if (file.is_regular_file()) { hasVideo = true; break; }
                    }
                }
                if (directoryHasImages(imageryDir) && !hasVideo)
                    incomplete.push_back(entry.path().filename().string());
            }
            std::sort(incomplete.begin(), incomplete.end());
            if (incomplete.empty()) continue;

            if (cfg.useOldImages) {
                const std::string& latest = incomplete.back();
                std::tm tm{};
                if (std::sscanf(latest.c_str(), "%4d%2d%2d_%2d%2d%2d",
                                &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
                    tm.tm_year -= 1900;
                    tm.tm_mon -= 1;
                    tm.tm_isdst = -1;
                    source.periodStart[interval] = std::mktime(&tm);
                    logInfo("Resuming " + name + " [" + interval + "] " + latest);
                }
                for (size_t i = 0; i + 1 < incomplete.size(); ++i)
                    std::filesystem::remove_all(comboDir / incomplete[i]);
            } else {
                for (const auto& key : incomplete)
                    std::filesystem::remove_all(comboDir / key);
            }
        }
    }

    bool running = true;
    while (running) {
        const std::time_t now = std::time(nullptr);
        const auto steadyNow = std::chrono::steady_clock::now();

        // Close every elapsed period for every source and queue one video per source.
        for (auto& source : sources) {
            const std::string name = comboName(source.config);
            for (const auto& interval : activeIntervals) {
                const std::time_t length = intervalLength(interval);
                while (now - source.periodStart[interval] >= length) {
                    const std::time_t endedPeriod = source.periodStart[interval];
                    const std::string key = periodKey(endedPeriod);
                    const auto periodDir = dataDir / source.config.satellite / interval / name / key;
                    const auto imageryDir = periodDir / "imagery";
                    const auto outputDir = periodDir / "output";

                    if (directoryHasImages(imageryDir)) {
                        logInfo("Queueing " + interval + " timelapse for " + name + " (" + key + ")");
                        const bool removeImages = cfg.deleteAfterTimelapse;
                        const std::string format = cfg.format;
                        const double keepInterval = cfg.keepInterval;
                        encoders.enqueue([imageryDir, outputDir, removeImages, format,
                                          keepInterval, endedPeriod]() {
                            compilePeriod(imageryDir, outputDir, removeImages, format,
                                          keepInterval, endedPeriod);
                        });
                    } else {
                        logWarn("No imagery for " + name + " [" + interval + "] " + key);
                    }
                    source.periodStart[interval] += length;
                }
                createRunDirectories(dataDir, source.config.satellite, interval, name,
                                     periodKey(source.periodStart[interval]));
            }
        }

        // Normally one source becomes due per spacing slot (one minute with 10 sources/10 minutes).
        for (auto& source : sources) {
            if (steadyNow < source.nextFetch) continue;

            const std::string name = comboName(source.config);
            const std::string url = imageUrl(source.config);
            if (url.empty()) {
                logError("Unsupported satellite provider: " + source.config.satellite);
                running = false;
                break;
            }
            if (!checkDiskSpace(dataDir.c_str(), cfg.minimumDiskGB)) {
                logError("Less than " + std::to_string(cfg.minimumDiskGB)
                         + " GB remaining on data path - stopping");
                running = false;
                break;
            }

            CURL* curl = curl_easy_init();
            if (!curl) {
                logError("Failed to initialise curl");
                running = false;
                break;
            }
            std::vector<char> imageBuffer;
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &imageBuffer);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

            const CURLcode result = curl_easy_perform(curl);
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            curl_easy_cleanup(curl);

            const bool validJpeg = imageBuffer.size() > 3
                && static_cast<unsigned char>(imageBuffer[0]) == 0xFF
                && static_cast<unsigned char>(imageBuffer[1]) == 0xD8
                && static_cast<unsigned char>(imageBuffer[2]) == 0xFF;

            if (result != CURLE_OK) {
                logError("Curl failed for " + name + ": " + curl_easy_strerror(result));
            } else if (httpCode != 200) {
                logWarn("HTTP " + std::to_string(httpCode) + " for " + name + " (" + url + ")");
            } else if (!validJpeg) {
                logWarn("Non-JPEG response for " + name + " ("
                        + std::to_string(imageBuffer.size()) + " bytes)");
            } else {
                const std::time_t capturedAt = std::time(nullptr);
                const std::string filename = periodKey(capturedAt) + ".jpg";
                for (const auto& interval : activeIntervals) {
                    const auto imagePath = dataDir / source.config.satellite / interval / name
                        / periodKey(source.periodStart[interval]) / "imagery" / filename;
                    std::ofstream file(imagePath, std::ios::binary);
                    file.write(imageBuffer.data(), static_cast<std::streamsize>(imageBuffer.size()));
                }
                logInfo("Saved " + name + " " + filename);
            }

            // Keep this source on its original cadence instead of drifting after slow downloads.
            do { source.nextFetch += cycle; }
            while (source.nextFetch <= std::chrono::steady_clock::now());
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    logInfo("=== GeoSatelliteView stopped ===");
    return 0;
}
