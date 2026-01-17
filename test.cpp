#include <iostream>
#include <ctime>
#include <filesystem>

bool pathExists(std::filesystem::path filePath) {
    if (std::filesystem::exists(filePath) && std::filesystem::is_directory(filePath)) {
        return true;
    } 

    return false;
}

int main() {
    time_t timestamp = time(NULL);
    struct tm datetime = *localtime(&timestamp);

    char dirDate[50];
    char fileTime[50];

    strftime(dirDate, 50, "%Y-%b-%d", &datetime);
    strftime(fileTime, 50, "%H-%M-%S", &datetime);

    std::string filePath = "./data/";
    std::cout << pathExists(std::filesystem::path(filePath)) << std::endl;

    return 0;
}