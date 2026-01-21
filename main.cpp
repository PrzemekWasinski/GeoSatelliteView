#include <curl/curl.h>
#include "timelapse.h"
#include <fstream>
#include <iostream>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <filesystem>
#include <thread>
#include <sys/statvfs.h>

bool pathExists(std::filesystem::path path) {
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        return true;
    } 

    return false;
}

size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = static_cast<std::ofstream*>(stream);
    out->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

bool checkDiskSpace(const char* path = "/mnt/ssd") {
    struct statvfs stat;

    if (statvfs(path, &stat) != 0) {
        std::cerr << "Error getting disk stats\n" << std::endl;
        return false;
    }

    unsigned long long available = (unsigned long long)stat.f_bavail * (unsigned long long)stat.f_frsize;
    unsigned long long availableGB = available / (1024ULL * 1024ULL * 1024ULL);

    std::cout << "Available space: " << availableGB << " GB\n";

    return availableGB >= 10;
}


int main() {
    bool firstRun = true;
    //start timestamp
    auto lastTimestamp = std::chrono::steady_clock::now();

    //start daystamp
    std::time_t t = std::time(nullptr);
    std::tm lastDay = *std::localtime(&t);
    int storedDay = lastDay.tm_yday; 
    int storedYear = lastDay.tm_year; 

    std::filesystem::path dataDir = "./data/";
    //std::filesystem::path dataDir = "/mnt/ssd/GeoSatelliteView/data/";

    std::filesystem::path goes16Path = std::string(dataDir) + "GOES16/";
    std::filesystem::path goes18Path = std::string(dataDir) + "GOES18/";

    if (!pathExists(goes16Path)) {
        std::filesystem::create_directory(goes16Path);
    } 

    if (!pathExists(goes18Path)) {
        std::filesystem::create_directory(goes18Path);
    } 

    while (true) {
        //check if theres enough disk space
        if (!checkDiskSpace()) {
            std::cout << "Not enough disk space left";
            break;
        }

        //geostationary satellites: 16, 18
        std::string satellites[2] = {"16", "18"};
        const std::string satellite = satellites[0]; //NOAA GOES16 

        //get current time and date
        time_t timestamp = time(NULL);
        char todayDate[50];
        char currentTime[50];

        struct tm datetime = *localtime(&timestamp);
        strftime(todayDate, 50, "%Y-%b-%d", &datetime);
        strftime(currentTime, 50, "%H-%M-%S", &datetime);

        auto currentTimestamp = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(currentTimestamp - lastTimestamp);

        //check if the direcotry for storing today's images has been created
        std::filesystem::path currentDir = std::string(std::string(dataDir) + "GOES" + satellite + "/" + todayDate);
        if (!pathExists(currentDir)) {
            std::filesystem::create_directory(currentDir);
        } 
        
        //create dir for timelapse output
        std::filesystem::path timelapseDir = std::string(std::string(dataDir) + "GOES" + satellite + "/timelapses");
        if (!pathExists(timelapseDir)) {
            std::filesystem::create_directory(timelapseDir);
        } 

        //check if day is over
        std::time_t now = std::time(nullptr);
        std::tm currentDay = *std::localtime(&now);

        if (currentDay.tm_yday != storedDay || currentDay.tm_year != storedYear) {
            std::time_t yesterdayTime = now - 86400;
            std::tm yesterdayTm = *std::localtime(&yesterdayTime);
            
            //format yesterday's date
            char yesterdayBuffer[50];
            strftime(yesterdayBuffer, 50, "%Y-%b-%d", &yesterdayTm);
            std::string yesterday = yesterdayBuffer;
            
            std::cout << "Creating timelapse for yesterday: " << yesterday << std::endl;
            
            //create timelapse for yesterday
            std::string yesterdayPath = std::string(dataDir) + "GOES" + satellite + "/" + yesterday;
            std::string timelapseOutput = std::string(timelapseDir) + "/" + yesterday + ".mp4";
            
            if (pathExists(yesterdayPath)) {
                //launch timelapse creation in a separate thread to not stop receiving images
                std::thread timelapseThread([yesterdayPath, timelapseOutput]() {
                    makeTimelapse(yesterdayPath, timelapseOutput, 24);
                });
                timelapseThread.detach();  
            }
            
            //update stored day
            storedDay = currentDay.tm_yday;
            storedYear = currentDay.tm_year;
        }
        
        //if 10 min has passed
        if (duration.count() >= 10 || firstRun) {
            //image save path
            std::filesystem::path imageSavePath = std::string(currentDir) + "/" + std::string(currentTime) + ".jpg";

            //save Image
            const std::string url = "https://cdn.star.nesdis.noaa.gov/GOES" + satellite + "/ABI/FD/GEOCOLOR/latest.jpg";

            CURL* curl = curl_easy_init();
            if (!curl) {
                std::cerr << "Failed to init curl\n";
                return 1;
            }

            std::ofstream file(imageSavePath, std::ios::binary);
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                std::cerr << "Error receiving GOES " + satellite + ": " 
                        << curl_easy_strerror(res) 
                        << " (" << todayDate << " " << currentTime << ")\n";

                file.close();
                std::filesystem::remove(imageSavePath);
            } else {
                std::cout << "Received GOES " + std::string(satellite) + " (" + std::string(todayDate) + " " + std::string(currentTime) + ")\n";
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