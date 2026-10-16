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
           root / "imagery" / "GOES19" / "07-10-2026");
    assert(runDirectory(root, "GOES19", "hourly", source, first, "output") ==
           root / "outputs" / "GOES19" / "hourly" / "07-10-2026_13-00-00");
    assert(videoPath(root, "GOES19", "daily", source, first, ".mp4") ==
           root / "outputs" / "GOES19" / "daily" / "07-10-2026_13-00-00" / (source + ".mp4"));
    assert(videoPath(root, "GOES19", "hourly", source, first, ".mp4") !=
           videoPath(root, "GOES19", "hourly", source, second, ".mp4"));
    assert(videoPath(root, "GOES19", "hourly", source, first, ".mp4").filename() ==
           source + ".mp4");
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
    // Every interval and configuration shares the date directory; JPEG names distinguish sources.
    assert(runDirectory(root, "GOES19", "hourly", source, first) ==
           runDirectory(root, "GOES19", "daily", source, second));
    assert(runDirectory(root, "GOES19", "hourly", source, first) !=
           runDirectory(root, "GOES19", "hourly", source, "2026-10-08_00-00-00"));
    std::filesystem::remove_all(root);
}
