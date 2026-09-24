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
    const std::string& dateStr
);

void createRunDirectories(
    const std::filesystem::path& dataDir,
    const std::string& satName,
    const std::string& interval,
    const std::string& comboName,
    const std::string& dateStr
);

#endif
