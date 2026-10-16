#include <curl/curl.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
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
#include <cctype>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <numeric>

#include "../include/timelapse.h"
#include "../include/fileFunctions.h"
#include "../include/configReader.h"
#include "../include/logger.h"
#include "../include/fetchSchedule.h"
#include "../config/satellites.h"

// Resize and encode each download once for the base collection.
static bool resizeDownloadedImage(const std::vector<char>& downloaded,
                                  std::vector<unsigned char>& encoded) {
    const cv::Mat image = cv::imdecode(downloaded, cv::IMREAD_COLOR);
    if (image.empty()) return false;
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(1080, 1080), 0, 0, cv::INTER_AREA);
    return cv::imencode(".jpg", resized, encoded, {cv::IMWRITE_JPEG_QUALITY, 90});
}

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
        if (config.sector == "M1" || config.sector == "M2" ||
            config.sector == "MESO1" || config.sector == "MESO2")
            return "https://cdn.star.nesdis.noaa.gov/GOES" + satNum + "/ABI/MESO/M"
                 + config.sector.back() + "/" + config.product + "/latest.jpg";
        std::string sector = config.sector;
        std::transform(sector.begin(), sector.end(), sector.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return "https://cdn.star.nesdis.noaa.gov/GOES" + satNum + "/ABI/SECTOR/"
             + sector + "/" + config.product + "/latest.jpg";
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

static std::string periodKey(std::time_t value, const char* format = "%Y-%m-%d_%H-%M-%S") {
    std::tm tm = *std::localtime(&value);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), format, &tm);
    return buffer;
}

// Called only after a download has been saved successfully. The first capture,
// not a rounded clock boundary, starts each interval's full-duration collection.
static void storeTemporaryCapture(const std::filesystem::path& tempDir,
    const std::string& satellite, const std::string& name,
    std::map<std::string, std::time_t>& starts, std::time_t captured,
    const std::filesystem::path& downloaded) {
    for (auto& [interval, start] : starts) {
        const auto actualStart = start == 0 ? captured : start;
        const auto directory = temporaryImageDirectory(tempDir, satellite, interval, name, periodKey(actualStart));
        std::filesystem::create_directories(directory);
        const auto destination = directory / downloaded.filename();
        std::error_code error;
        std::filesystem::create_hard_link(downloaded, destination, error);
        if (error) std::filesystem::copy_file(downloaded, destination, std::filesystem::copy_options::skip_existing);
        if (start == 0) {
            start = captured;
            logInfo("Collection started: " + name + " [" + interval + "] " + periodKey(start));
        }
    }
}

// Encode on the project disk, then copy the finished video to permanent storage.
// No cleanup operation in this function targets the permanent output directory.
bool compilePeriod(const std::vector<std::string>& frames,
                   const std::filesystem::path& temporary,
                   const std::filesystem::path& outputPath, const std::string& format,
                   bool videos = false) {
    try {
        std::filesystem::create_directories(temporary.parent_path());
        if (!(videos ? concatenateTimelapses(frames, temporary.string())
                     : makeTimelapse(frames, temporary.string(), 24, format))) {
            logError("Timelapse failed; keeping temporary imagery: " + temporary.string());
            return false;
        }
        std::filesystem::create_directories(outputPath.parent_path());
        // Existing permanent files are preserved. A collision keeps the temporary job for review.
        if (std::filesystem::exists(outputPath)) {
            logWarn("Output already exists; keeping temporary job: " + outputPath.string());
            return false;
        }
        const auto staging = std::filesystem::path(outputPath.string() + ".partial");
        std::filesystem::copy_file(temporary, staging, std::filesystem::copy_options::overwrite_existing);
        // Hard-link publication is atomic and refuses to replace existing outputs.
        std::filesystem::create_hard_link(staging, outputPath);
        std::filesystem::remove(staging);
        logInfo("Timelapse done: " + outputPath.string());
        std::filesystem::remove(temporary);
        std::set<std::filesystem::path> completedFolders;
        if (!videos) for (const auto& frame : frames) completedFolders.insert(std::filesystem::path(frame).parent_path());
        for (const auto& folder : completedFolders) std::filesystem::remove_all(folder);
        // Encoding jobs also have their own period folder.
        std::error_code ignored;
        std::filesystem::remove(temporary.parent_path(), ignored);
        return true;
    } catch (const std::exception& error) {
        logError("Timelapse failed; keeping temporary job: " + std::string(error.what()));
        return false;
    }
}

static std::time_t temporaryImageTime(const std::string& filename) {
    std::tm tm{};
    if (std::sscanf(filename.c_str(), "%2d-%2d-%4d_%2d-%2d-%2d",
                    &tm.tm_mday, &tm.tm_mon, &tm.tm_year,
                    &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return -1;
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

static std::vector<std::pair<std::time_t, std::string>> sourceFrames(
    const std::filesystem::path& tempDir, const std::string& satellite,
    const std::string& interval, const std::string& name,
    const std::string& key = "") {
    std::vector<std::pair<std::time_t, std::string>> frames;
    const auto root = tempDir / satellite / interval;
    if (!pathExists(root)) return frames;
    for (const auto& date : std::filesystem::directory_iterator(root)) {
        if (!key.empty() && date.path() != temporaryImageDirectory(tempDir, satellite, interval, name, key).parent_path()) continue;
        const auto configDir = date.path() / name;
        if (!date.is_directory() || !pathExists(configDir)) continue;
        for (const auto& file : std::filesystem::directory_iterator(configDir)) {
            if (!file.is_regular_file() || file.path().extension() != ".jpg") continue;
            const auto captured = temporaryImageTime(file.path().filename().string());
            if (captured != -1) frames.emplace_back(captured, file.path().string());
        }
    }
    std::sort(frames.begin(), frames.end());
    return frames;
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

struct VideoGroup {
    std::time_t start = 0;
    std::vector<std::string> clips;
};

struct PendingCompilation {
    std::vector<std::string> clips;
    std::filesystem::path temporary, output;
};

struct SourceState {
    std::map<std::string, VideoGroup> groups;
    SatelliteConfig config;
    std::map<std::string, std::time_t> periodStart;
    FetchSchedule schedule;
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
    if (!cfg.hourly && !cfg.daily && !cfg.threeDaily && !cfg.weekly && !cfg.monthly) {
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
    if (cfg.threeDaily) activeIntervals.push_back("3daily");
    if (cfg.weekly)  activeIntervals.push_back("weekly");
    if (cfg.monthly) activeIntervals.push_back("monthly");
    auto intervalLength = [](const std::string& interval) -> std::time_t {
        if (interval == "hourly") return 3600;
        if (interval == "daily") return 86400;
        if (interval == "3daily") return 3 * 86400;
        if (interval == "weekly") return 7 * 86400;
        return 30 * 86400;
    };

    // A common base also handles periods such as three days and seven days.
    std::time_t baseLength = intervalLength(activeIntervals.front());
    for (const auto& interval : activeIntervals)
        baseLength = std::gcd(baseLength, intervalLength(interval));
    std::string baseInterval = baseLength == 3600 ? "hourly" :
        baseLength == 86400 ? "daily" : activeIntervals.front();
    std::vector<PendingCompilation> pendingCompilations;


    std::filesystem::path dataDir = std::filesystem::path(cfg.dataPath).lexically_normal();
    if (dataDir.filename().empty()) dataDir = dataDir.parent_path();
    if (dataDir.filename() != "data" && dataDir.filename() != "Data") dataDir /= "Data";
    std::filesystem::create_directories(dataDir / "imagery");
    std::filesystem::create_directories(dataDir / "outputs");
    const auto tempDir = std::filesystem::current_path() / "temp";
    if (std::filesystem::is_symlink(tempDir)) {
        logError("Project temp/ must be a real directory, not a symbolic link");
        return 1;
    }
    const auto permanentRelative = std::filesystem::weakly_canonical(dataDir).lexically_relative(
        std::filesystem::weakly_canonical(tempDir));
    if (permanentRelative.empty() || *permanentRelative.begin() != "..") {
        logError("DataPath must be outside the project's temporary directory");
        return 1;
    }
    std::filesystem::remove_all(tempDir);
    std::filesystem::create_directories(tempDir);
    logInfo("Cleared project temp/; starting a fresh collection");


    const auto cycle = std::chrono::seconds(cfg.pullIntervalMinutes * 60);
    const auto spacing = std::chrono::seconds(
        std::max<long long>(1, cycle.count() / static_cast<long long>(SATELLITE_LIST.size())));
    const auto scheduleStart = std::chrono::steady_clock::now();

    std::vector<SourceState> sources;
    sources.reserve(SATELLITE_LIST.size());
    for (size_t i = 0; i < SATELLITE_LIST.size(); ++i) {
        SourceState state;
        state.config = SATELLITE_LIST[i];
        state.schedule.nextFetch = scheduleStart + spacing * static_cast<long long>(i);
        state.schedule.slot = state.schedule.nextFetch;
        state.periodStart[baseInterval] = 0;
        sources.push_back(std::move(state));
        logInfo("Scheduled " + comboName(SATELLITE_LIST[i]) + " at +"
                + std::to_string(spacing.count() * static_cast<long long>(i))
                + " seconds, then every " + std::to_string(cfg.pullIntervalMinutes) + " minutes");
    }

    logInfo("Active sources: " + std::to_string(sources.size()));
    logInfo("Encoder workers: " + std::to_string(cfg.encoderWorkers));
    EncodingQueue encoders(static_cast<size_t>(cfg.encoderWorkers));

    // Restore periodic-image slots from permanent files without modifying them.
    std::map<std::string, std::set<std::time_t>> archivedSlots;
    const auto keepSeconds = static_cast<std::time_t>(cfg.keepInterval * 3600);
    if (keepSeconds > 0) {
        for (const auto& source : sources) {
            const auto name = comboName(source.config);
            const auto root = dataDir / "imagery" / source.config.satellite;
            if (!pathExists(root)) continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
                const auto filename = entry.path().filename().string();
                if (!entry.is_regular_file() || entry.path().extension() != ".jpg" ||
                    filename.compare(0, name.size() + 1, name + "_") != 0) continue;
                const auto captured = parseImageTime(filename.substr(name.size() + 1));
                if (captured != -1) archivedSlots[name].insert(captured / keepSeconds);
            }
        }
    }
    auto archiveImage = [&](const SourceState& source, std::time_t captured,
                            const std::filesystem::path& temporary) {
        if (keepSeconds <= 0) return true;
        const auto name = comboName(source.config);
        const auto slot = captured / keepSeconds;
        if (archivedSlots[name].count(slot)) return true;
        try {
            const auto directory = runDirectory(dataDir, source.config.satellite, "", name, periodKey(captured));
            std::filesystem::create_directories(directory);
            const auto destination = directory / (name + "_" + periodKey(captured, "%Y%m%d_%H%M%S") + ".jpg");
            std::filesystem::copy_file(temporary, destination, std::filesystem::copy_options::skip_existing);
            archivedSlots[name].insert(slot);
            return true;
        } catch (const std::exception& error) {
            logError("Periodic image copy failed; keeping temporary frames: " + std::string(error.what()));
            return false;
        }
    };

    auto closePeriods = [&](std::time_t now) {
        for (auto& source : sources) {
            const std::string name = comboName(source.config);
            for (const auto& [interval, unused] : source.periodStart) {
                const std::time_t length = baseLength;
                while (source.periodStart[interval] != 0 && now - source.periodStart[interval] >= length) {
                    const std::time_t endedPeriod = source.periodStart[interval];
                    const std::string key = periodKey(endedPeriod);
                    const auto outputFile = videoPath(dataDir, source.config.satellite, interval, name, key,
                                                      formatExtension(cfg.format));
                    std::vector<std::string> frames;
                    bool archived = true;
                    for (const auto& [captured, path] : sourceFrames(tempDir, source.config.satellite, interval, name, key)) {
                        if (captured >= endedPeriod && captured < endedPeriod + length) {
                            frames.push_back(path);
                            if (!archiveImage(source, captured, path)) archived = false;
                        }
                    }
                    if (archived && !frames.empty() && !std::filesystem::is_regular_file(outputFile)) {
                        const std::string format = cfg.format;
                        const auto temporary = tempDir / ".encoding" / source.config.satellite / interval / key
                                               / (name + formatExtension(format));
                        encoders.enqueue([frames, temporary, outputFile, format]() {
                            compilePeriod(frames, temporary, outputFile, format);
                        });
                    }
                    // Longer periods reference base outputs, including jobs still encoding.
                    for (const auto& longer : activeIntervals) {
                        if (intervalLength(longer) == baseLength) continue;
                        auto& group = source.groups[longer];
                        if (group.clips.empty()) group.start = endedPeriod;
                        group.clips.push_back(outputFile.string());
                        if (group.clips.size() == static_cast<size_t>(intervalLength(longer) / baseLength)) {
                            const auto groupKey = periodKey(group.start);
                            const auto output = videoPath(dataDir, source.config.satellite, longer, name,
                                                          groupKey, formatExtension(cfg.format));
                            const auto temporary = tempDir / ".encoding" / source.config.satellite / longer
                                / groupKey / (name + formatExtension(cfg.format));
                            pendingCompilations.push_back({std::move(group.clips), temporary, output});
                            group = {};
                        }
                    }
                    source.periodStart[interval] = endedPeriod + length;
                }

            }
        }

    };

    bool running = true;
    while (running) {
        closePeriods(std::time(nullptr));
        for (auto job = pendingCompilations.begin(); job != pendingCompilations.end();) {
            if (!std::all_of(job->clips.begin(), job->clips.end(), [](const auto& clip) {
                    return std::filesystem::is_regular_file(clip);
                })) { ++job; continue; }
            const auto ready = *job;
            const auto format = cfg.format;
            encoders.enqueue([ready, format]() {
                compilePeriod(ready.clips, ready.temporary, ready.output, format, true);
            });
            job = pendingCompilations.erase(job);
        }

        // Normally one source becomes due per spacing slot (one minute with 10 sources/10 minutes).
        for (auto& source : sources) {
            if (std::chrono::steady_clock::now() < source.schedule.nextFetch) continue;

            const std::string name = comboName(source.config);
            const std::string url = imageUrl(source.config);
            if (url.empty()) {
                logError("Unsupported satellite provider: " + source.config.satellite);
                running = false;
                break;
            }
            if (!checkDiskSpace(dataDir.c_str(), cfg.minimumDiskGB) ||
                !checkDiskSpace(tempDir.c_str(), cfg.minimumDiskGB)) {
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

            const auto requestStart = std::chrono::steady_clock::now();
            const CURLcode result = curl_easy_perform(curl);
            long httpCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
            curl_easy_cleanup(curl);
            logInfo("Fetch " + name + " HTTP " + std::to_string(httpCode)
                    + " in " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - requestStart).count())
                    + " ms, " + std::to_string(imageBuffer.size()) + " bytes");
            bool saved = false;

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
                try {
                    std::vector<unsigned char> resizedJpeg;
                    if (!resizeDownloadedImage(imageBuffer, resizedJpeg)) {
                        logWarn("Could not decode or resize JPEG for " + name);
                    } else {
                        const std::time_t capturedAt = std::time(nullptr);
                        // Close elapsed collections before assigning this capture to its periods.
                        closePeriods(capturedAt);
                        const auto filename = periodKey(capturedAt, "%d-%m-%Y_%H-%M-%S") + ".jpg";
                        const auto downloadDir = tempDir / ".downloads" / name;
                        std::filesystem::create_directories(downloadDir);
                        const auto firstImage = downloadDir / filename;
                        std::ofstream file(firstImage, std::ios::binary);
                        file.write(reinterpret_cast<const char*>(resizedJpeg.data()),
                                   static_cast<std::streamsize>(resizedJpeg.size()));
                        file.close();
                        saved = static_cast<bool>(file);
                        if (saved) {
                            storeTemporaryCapture(tempDir, source.config.satellite, name,
                                                  source.periodStart, capturedAt, firstImage);
                            if (archiveImage(source, capturedAt, firstImage)) {
                                std::filesystem::remove(firstImage);
                                std::error_code ignored;
                                std::filesystem::remove(downloadDir, ignored);
                            } else saved = false;
                        }
                        if (saved) logInfo("Saved temporary imagery: " + name + " " + filename);
                    }
                } catch (const std::exception& error) {
                    saved = false;
                    logWarn("Image processing failed for " + name + ": " + error.what());
                }
            }

            const auto finishedAt = std::chrono::steady_clock::now();
            source.schedule.finish(saved, finishedAt, cycle);
            if (!saved) {
                logWarn("No image saved for " + name + "; "
                        + (source.schedule.retries ? "retry" : "next scheduled attempt")
                        + " in " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                            source.schedule.nextFetch - finishedAt).count()) + " seconds");
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    logInfo("=== GeoSatelliteView stopped ===");
    return 0;
}
