#include "../include/fileFunctions.h"
#include "../include/configReader.h"
#include <cassert>
#include <fstream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "geosatellite-storage-test";
    const std::string source = "GOES19-FD-GEOCOLOR";
    const std::string first = "2026-10-07_13-00-00";
    const std::string second = "2026-10-07_14-00-00";
    assert(runDirectory(root, "GOES19", "hourly", source, first) ==
           root / "imagery" / "hourly" / first / "GOES19" / source);
    assert(runDirectory(root, "GOES19", "hourly", source, first, "output") ==
           root / "output" / "hourly" / first / "GOES19" / source);
    for (const auto& interval : {"hourly", "daily", "3daily", "weekly", "monthly"}) {
        createRunDirectories(root, "GOES19", interval, source, first);
        assert(pathExists(runDirectory(root, "GOES19", interval, source, first)));
        assert(pathExists(runDirectory(root, "GOES19", interval, source, first, "output")));
    }
    createRunDirectories(root, "GOES19", "hourly", source, second);
    const auto configPath = root / "config.yml";
    { std::ofstream f(configPath); f << "Hourly: True\nDaily: False\nThreeDaily: True\n"; }
    auto config = readConfig(configPath.string());
    assert(config.hourly && !config.daily && config.threeDaily);
    // Removing one source period must leave adjacent periods and outputs intact.
    std::filesystem::remove_all(runDirectory(root, "GOES19", "hourly", source, first));
    assert(pathExists(runDirectory(root, "GOES19", "hourly", source, second)));
    assert(pathExists(runDirectory(root, "GOES19", "hourly", source, first, "output")));
    std::filesystem::remove_all(root);
}
