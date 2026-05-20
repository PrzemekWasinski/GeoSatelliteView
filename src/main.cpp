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

#include "../include/timelapse.h"
#include "../include/fileFunctions.h"

size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = static_cast<std::ofstream*>(stream);
    out->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool checkDiskSpace(const char* path = "/mnt/ssd") {
    struct statvfs stat;
    if (statvfs(path, &stat) != 0) {
        std::cerr << "Error getting disk stats\n";
        return false;
    }
    unsigned long long available = (unsigned long long)stat.f_bavail * (unsigned long long)stat.f_frsize;
    unsigned long long availableGB = available / (1024ULL * 1024ULL * 1024ULL);
    std::cout << "Available space: " << availableGB << " GB\n";
    return availableGB >= 10;
}

// Compile timelapse then remove imagery folder
void compilePeriod(const std::filesystem::path& imageryDir, const std::filesystem::path& outputDir) {
    std::string outputFile = std::string(outputDir) + "/output.mkv";
    std::filesystem::create_directories(outputDir);
    makeTimelapse(std::string(imageryDir), outputFile, 24);
}

int main() {
    // --- Parameters ---
    const std::string satellite  = "16";       // "16" or "18"
    const std::string sector     = "FD";
    const std::string product    = "GEOCOLOR";
    const std::string interval   = "daily";    // "hourly", "daily", "weekly", "monthly"

    std::filesystem::path dataDir = "./data/";
    //std::filesystem::path dataDir = "/mnt/ssd/GeoSatelliteView/data/";

    const std::string satName  = "GOES" + satellite;
    const std::string comboName = satName + "-" + sector + "-" + product;
    const std::string imageUrl  = "https://cdn.star.nesdis.noaa.gov/GOES" + satellite
                                + "/ABI/" + (sector == "FD" ? "FD" : "SECTOR/" + sector)
                                + "/" + product + "/latest.jpg";

    bool firstRun = true;
    auto lastTimestamp = std::chrono::steady_clock::now();

    std::time_t t = std::time(nullptr);
    std::tm last = *std::localtime(&t);
    int storedHour  = last.tm_hour;
    int storedDay   = last.tm_yday;
    int storedYear  = last.tm_year;
    int storedWeek  = last.tm_yday / 7;
    int storedMonth = last.tm_mon;

    while (true) {
        if (!checkDiskSpace(".")) {
            std::cout << "Not enough disk space left\n";
            break;
        }

        // Current time strings
        time_t now = time(nullptr);
        struct tm dt = *localtime(&now);
        char dateStr[20], timeStr[20], dateTimeStr[40];
        strftime(dateStr,     sizeof(dateStr),     "%d-%m-%Y",    &dt);
        strftime(timeStr,     sizeof(timeStr),     "%H-%M-%S",    &dt);
        snprintf(dateTimeStr, sizeof(dateTimeStr), "%s_%s", timeStr, dateStr);

        // Build paths for this run
        std::filesystem::path comboDir  = dataDir / satName / interval / comboName;
        std::filesystem::path dateDir   = comboDir / dateStr;
        std::filesystem::path imageryDir = dateDir / "imagery";
        std::filesystem::path outputDir  = dateDir / "output";

        createRunDirectories(dataDir, satName, interval, comboName, dateStr);

        // Check if the current period has rolled over — if so, compile timelapse for the last period
        std::tm cur = *std::localtime(&now);
        bool periodOver = false;
        std::string prevDate;

        if (interval == "hourly" && cur.tm_hour != storedHour) {
            periodOver = true;
            std::time_t prev = now - 3600;
            std::tm prevTm = *std::localtime(&prev);
            char buf[20]; strftime(buf, sizeof(buf), "%d-%m-%Y", &prevTm);
            prevDate = buf;
            storedHour = cur.tm_hour;
        } else if (interval == "daily" && (cur.tm_yday != storedDay || cur.tm_year != storedYear)) {
            periodOver = true;
            std::time_t prev = now - 86400;
            std::tm prevTm = *std::localtime(&prev);
            char buf[20]; strftime(buf, sizeof(buf), "%d-%m-%Y", &prevTm);
            prevDate = buf;
            storedDay  = cur.tm_yday;
            storedYear = cur.tm_year;
        } else if (interval == "weekly" && (cur.tm_yday / 7) != storedWeek) {
            periodOver = true;
            std::time_t prev = now - 7 * 86400;
            std::tm prevTm = *std::localtime(&prev);
            char buf[20]; strftime(buf, sizeof(buf), "%d-%m-%Y", &prevTm);
            prevDate = buf;
            storedWeek = cur.tm_yday / 7;
        } else if (interval == "monthly" && cur.tm_mon != storedMonth) {
            periodOver = true;
            std::time_t prev = now - 28 * 86400;
            std::tm prevTm = *std::localtime(&prev);
            char buf[20]; strftime(buf, sizeof(buf), "%d-%m-%Y", &prevTm);
            prevDate = buf;
            storedMonth = cur.tm_mon;
        }

        if (periodOver && !prevDate.empty()) {
            std::filesystem::path prevImagery = comboDir / prevDate / "imagery";
            std::filesystem::path prevOutput  = comboDir / prevDate / "output";
            if (pathExists(prevImagery)) {
                std::thread([prevImagery, prevOutput]() {
                    compilePeriod(prevImagery, prevOutput);
                }).detach();
            }
        }

        // Fetch image every 10 minutes
        auto currentTimestamp = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(currentTimestamp - lastTimestamp);

        if (elapsed.count() >= 10 || firstRun) {
            std::filesystem::path imagePath = imageryDir / (std::string(dateTimeStr) + ".jpg");

            CURL* curl = curl_easy_init();
            if (!curl) {
                std::cerr << "Failed to init curl\n";
                return 1;
            }

            std::ofstream file(imagePath, std::ios::binary);
            curl_easy_setopt(curl, CURLOPT_URL, imageUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "Curl error for " << comboName << ": " << curl_easy_strerror(res) << "\n";
                file.close();
                std::filesystem::remove(imagePath);
            } else {
                std::cout << "Saved " << comboName << " " << dateTimeStr << "\n";
            }

            curl_easy_cleanup(curl);
            file.close();

            lastTimestamp = currentTimestamp;
            firstRun = false;
        }

        std::this_thread::sleep_for(std::chrono::seconds(30));
    }

    return 0;
}
