#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <filesystem>

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

time_t timestamp = time(NULL);
char todayDate[50];
char currentTime[50];

int main() {
    auto lastTimestamp = std::chrono::steady_clock::now();
    bool firstRun = true;

    while (true) {
        //geostationary satellites: 16, 18
        const std::string satellite = "16";

        //get current time and date
        struct tm datetime = *localtime(&timestamp);
        strftime(todayDate, 50, "%Y-%b-%d", &datetime);
        strftime(currentTime, 50, "%H-%M-%S", &datetime);

        auto currentTimestamp = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(currentTimestamp - lastTimestamp);

        //check if the direcotry for storing today's images has been created
        std::filesystem::path currentDir = "./data/" + std::string("GOES" + satellite + "/" + todayDate);
        if (!pathExists(currentDir)) {
            std::filesystem::create_directory(currentDir);
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
                std::cerr << "Error receiving GOES " + satellite + ": " << curl_easy_strerror(res) << "\n";
            } else {
                std::cout << "Received GOES " + std::string(satellite) + " (" + std::string(todayDate) + " " + std::string(currentTime) + ")\n";
            }

            curl_easy_cleanup(curl);
            file.close();

            lastTimestamp = currentTimestamp;
            firstRun = false;
        }
    }   

    return 0;
}
