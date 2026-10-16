#ifndef FILE_FUNCTIONS_H
#define FILE_FUNCTIONS_H

#include <filesystem>
#include <string>

bool pathExists(const std::filesystem::path& path);

std::filesystem::path runDirectory(
    const std::filesystem::path& dataDir,
    const std::string& satName,
    const std::string& interval,
    const std::string& comboName,
    const std::string& dateStr,
    const std::string& kind = "imagery"
);

void createRunDirectories(
    const std::filesystem::path& dataDir,
    const std::string& satName,
    const std::string& interval,
    const std::string& comboName,
    const std::string& dateStr
);

std::filesystem::path videoPath(
    const std::filesystem::path& dataDir, const std::string& satName,
    const std::string& interval, const std::string& comboName,
    const std::string& dateStr, const std::string& extension
);

std::filesystem::path temporaryImageDirectory(
    const std::filesystem::path& tempDir, const std::string& satellite,
    const std::string& interval, const std::string& name, const std::string& periodKey);

#endif
