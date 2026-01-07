#include <curl/curl.h>
#include <fstream>
#include <iostream>

size_t write_data(void* ptr, size_t size, size_t nmemb, void* stream) {
    std::ofstream* out = static_cast<std::ofstream*>(stream);
    out->write(static_cast<char*>(ptr), size * nmemb);
    return size * nmemb;
}

int main() {
    //Geostationary satellites: 16, 17, 18, 19
    const std::string satellite = "16";
    const std::string url = "https://cdn.star.nesdis.noaa.gov/GOES" + satellite + "/ABI/FD/GEOCOLOR/latest.jpg";

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to init curl\n";
        return 1;
    }

    std::ofstream file("data/goes.jpg", std::ios::binary);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "curl error: " << curl_easy_strerror(res) << "\n";
    } else {
        std::cout << "Downloaded goes.jpg\n";
    }

    curl_easy_cleanup(curl);
    file.close();

    return 0;
}
