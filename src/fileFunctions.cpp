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
    const std::string date = dateStr.substr(8, 2) + "-" + dateStr.substr(5, 2)
                           + "-" + dateStr.substr(0, 4);
    if (kind == "output")
        return dataDir / "outputs" / satName / interval / (date + "_" + dateStr.substr(11));
    return dataDir / "imagery" / satName / date;
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

std::filesystem::path videoPath(
    const std::filesystem::path& dataDir, const std::string& satName,
    const std::string& interval, const std::string& comboName,
    const std::string& dateStr, const std::string& extension
) {
    return runDirectory(dataDir, satName, interval, comboName, dateStr, "output")
           / (comboName + extension);
}

std::filesystem::path temporaryImageDirectory(
    const std::filesystem::path& tempDir, const std::string& satellite,
    const std::string& interval, const std::string& name, const std::string& periodKey) {
    const auto date = periodKey.substr(8, 2) + "-" + periodKey.substr(5, 2) + "-" + periodKey.substr(0, 4);
    return tempDir / satellite / interval / (date + "_" + periodKey.substr(11)) / name;
}
