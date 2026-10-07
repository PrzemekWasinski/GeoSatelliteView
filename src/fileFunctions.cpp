#include "../include/fileFunctions.h"

bool pathExists(const std::filesystem::path& path) {
    return std::filesystem::exists(path) && std::filesystem::is_directory(path);
}

std::filesystem::path runDirectory(
    const std::filesystem::path& dataDir,
    const std::string& satName,
    const std::string& interval,
    const std::string& comboName,
    const std::string& dateStr,
    const std::string& kind
) {
    return dataDir / kind / interval / dateStr / satName / comboName;
}

void createRunDirectories(
    const std::filesystem::path& dataDir,
    const std::string& satName,
    const std::string& interval,
    const std::string& comboName,
    const std::string& dateStr
) {
    std::filesystem::path imageryDir = runDirectory(dataDir, satName, interval, comboName, dateStr);
    std::filesystem::path outputDir  = runDirectory(dataDir, satName, interval, comboName, dateStr, "output");
    std::filesystem::create_directories(imageryDir);
    std::filesystem::create_directories(outputDir);
}
