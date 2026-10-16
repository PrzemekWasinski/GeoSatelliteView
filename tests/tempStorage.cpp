#define main collectorMain
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <opencv2/videoio.hpp>

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("geosat-temp-storage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto temporary = root / "project" / "temp";
    const auto permanent = root / "archive";
    const std::string name = "GOES18-FD-GEOCOLOR";
    const std::string key = "2026-10-16_09-00-00";
    const auto hourly = temporaryImageDirectory(temporary, "GOES18", "hourly", name, key);
    const auto daily = temporaryImageDirectory(temporary, "GOES18", "daily", name, key);
    std::filesystem::create_directories(hourly);
    std::filesystem::create_directories(daily);
    const auto frame = hourly / "16-10-2026_09-10-00.jpg";
    const auto linked = daily / frame.filename();
    assert(cv::imwrite(frame.string(), cv::Mat(1080, 1080, CV_8UC3, cv::Scalar(20, 100, 200))));
    std::filesystem::create_hard_link(frame, linked);
    const auto archive = permanent / "imagery" / "GOES18" / "16-10-2026" / (name + "_20261016_091000.jpg");
    std::filesystem::create_directories(archive.parent_path());
    std::filesystem::copy_file(frame, archive);
    const auto archiveSize = std::filesystem::file_size(archive);
    assert(sourceFrames(temporary, "GOES18", "hourly", name).size() == 1);
    assert(sourceFrames(temporary, "GOES18", "daily", name).size() == 1);
    const auto nextHour = temporaryImageDirectory(temporary, "GOES18", "hourly", name, "2026-10-16_10-00-00");
    std::filesystem::create_directories(nextHour);
    std::filesystem::copy_file(frame, nextHour / "16-10-2026_10-10-00.jpg");
    const auto hourlyVideo = videoPath(permanent, "GOES18", "hourly", name, key, ".mp4");
    assert(compilePeriod({frame.string()}, temporary / ".encoding" / "hourly.mp4", hourlyVideo, "MP4"));
    assert(!std::filesystem::exists(hourly));
    assert(std::filesystem::exists(nextHour / "16-10-2026_10-10-00.jpg"));
    assert(std::filesystem::exists(linked));
    assert(std::filesystem::file_size(archive) == archiveSize);
    const auto videoSize = std::filesystem::file_size(hourlyVideo);
    // A permanent-file collision must not overwrite the output or remove pending input.
    assert(!compilePeriod({linked.string()}, temporary / ".encoding" / "collision.mp4", hourlyVideo, "MP4"));
    assert(std::filesystem::file_size(hourlyVideo) == videoSize);
    assert(std::filesystem::exists(linked));
    const auto dailyVideo = videoPath(permanent, "GOES18", "daily", name, key, ".mp4");
    assert(compilePeriod({linked.string()}, temporary / ".encoding" / "daily.mp4", dailyVideo, "MP4"));
    assert(!std::filesystem::exists(daily));
    assert(std::filesystem::file_size(archive) == archiveSize);
    assert(std::filesystem::file_size(hourlyVideo) == videoSize);
    assert(!compilePeriod({}, temporary / ".encoding" / "empty.mp4", permanent / "failed.mp4", "MP4"));
    assert(!std::filesystem::exists(permanent / "failed.mp4"));
    // Concatenation preserves frame count/order and never cleans up source videos.
    const auto blue = root / "blue.jpg";
    assert(cv::imwrite(blue.string(), cv::Mat(1080, 1080, CV_8UC3, cv::Scalar(230, 10, 10))));
    const auto secondVideo = permanent / "second 'clip.mp4";
    assert(makeTimelapse(std::vector<std::string>{blue.string()}, secondVideo.string(), 24, "MP4"));
    const auto combined = permanent / "combined.mp4";
    assert(compilePeriod({hourlyVideo.string(), secondVideo.string()},
        temporary / ".encoding" / "combined.mp4", combined, "MP4", true));
    assert(std::filesystem::exists(hourlyVideo));
    assert(std::filesystem::exists(secondVideo));
    cv::VideoCapture video(combined.string());
    cv::Mat decoded;
    assert(video.read(decoded));
    assert(cv::mean(decoded)[2] > cv::mean(decoded)[0]);
    assert(video.read(decoded));
    assert(cv::mean(decoded)[0] > cv::mean(decoded)[2]);
    assert(!video.read(decoded));
    video.release();
    assert(!concatenateTimelapses({}, (root / "empty.mp4").string()));
    assert(!concatenateTimelapses({(root / "missing.mp4").string()}, (root / "missing-output.mp4").string()));
    // Regression: a first successful capture must populate temp immediately, for all
    // enabled intervals, using its exact time rather than the next clock boundary.
    std::tm firstTime{};
    firstTime.tm_year = 126; firstTime.tm_mon = 9; firstTime.tm_mday = 16;
    firstTime.tm_hour = 11; firstTime.tm_min = 53; firstTime.tm_sec = 27; firstTime.tm_isdst = -1;
    const auto captured = std::mktime(&firstTime);
    const auto staging = temporary / ".downloads" / "capture";
    std::filesystem::create_directories(staging);
    const auto downloaded = staging / (periodKey(captured, "%d-%m-%Y_%H-%M-%S") + ".jpg");
    std::filesystem::copy_file(archive, downloaded);
    std::map<std::string, std::time_t> starts{{"hourly", 0}, {"daily", 0}};
    storeTemporaryCapture(temporary, "GOES19", name, starts, captured, downloaded);
    std::filesystem::remove(downloaded);
    for (const auto& [interval, start] : starts) {
        assert(start == captured);
        const auto directory = temporary / "GOES19" / interval / "16-10-2026_11-53-27" / name;
        assert(std::filesystem::exists(directory / "16-10-2026_11-53-27.jpg"));
        const auto frames = sourceFrames(temporary, "GOES19", interval, name);
        assert(frames.size() == 1 && frames.front().first == captured);
        assert(videoPath(permanent, "GOES19", interval, name, periodKey(start), ".mp4").parent_path()
               == permanent / "outputs" / "GOES19" / interval / "16-10-2026_11-53-27");
    }
    std::filesystem::remove_all(root);
}
