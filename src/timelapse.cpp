#include <opencv2/opencv.hpp>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>

#include "../include/timelapse.h"

bool makeTimelapse(const std::string& folderPath, const std::string& outputFile, int fps) {
    std::vector<std::string> imageFiles;

    //collect image paths
    for (const auto& entry : std::filesystem::directory_iterator(folderPath)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".jpg" || ext == ".png" || ext == ".jpeg") {
                imageFiles.push_back(entry.path().string());
            }
        }
    }

    //sort images alphabetically (filenames are ISO-ordered, so this is chronological)
    std::sort(imageFiles.begin(), imageFiles.end());

    if (imageFiles.empty()) {
        std::cerr << "No images found" << std::endl;
        return false;
    }

    //find the first image that actually decodes - corrupt/incomplete downloads are skipped
    cv::Mat firstFrame;
    for (const auto& file : imageFiles) {
        firstFrame = cv::imread(file);
        if (!firstFrame.empty()) break;
    }
    if (firstFrame.empty()) {
        std::cerr << "No decodable images found in " << folderPath << std::endl;
        return false;
    }

    //resize if too large
    int maxDim = 2048;
    cv::Size frameSize = firstFrame.size();

    if (firstFrame.cols > maxDim || firstFrame.rows > maxDim) {
        double scale = std::min((double)maxDim / firstFrame.cols,
                                (double)maxDim / firstFrame.rows);
        frameSize = cv::Size(firstFrame.cols * scale, firstFrame.rows * scale);
        std::cout << "Resizing from " << firstFrame.size() << " to " << frameSize << std::endl;
    }

    cv::VideoWriter writer(outputFile,
                       cv::VideoWriter::fourcc('M','J','P','G'),
                       fps,
                       frameSize);

    if (!writer.isOpened()) {
        std::cerr << "Failed to open video writer!" << std::endl;
        return false;
    }

    //write frames, skipping any that fail to decode
    int written = 0;
    for (const auto& file : imageFiles) {
        cv::Mat img = cv::imread(file);
        if (!img.empty()) {
            //resize if needed
            if (img.size() != frameSize) {
                cv::resize(img, img, frameSize);
            }
            writer.write(img);
            ++written;
            //"." for each frame
            std::cout << "." << std::flush;
        }
    }

    writer.release();
    if (written == 0) {
        std::cerr << "\nNo frames written for " << folderPath << std::endl;
        return false;
    }
    std::cout << "\nTimelapse saved to " << outputFile << std::endl;
    return true;
}

//main function for testing with custom input/output paths
// int main() {
//     std::string dataPath = "/mnt/ssd/GeoSatelliteView/data/";
//     //std::string dataPath = "./data/";
//     makeTimelapse(dataPath + "GOES16/2026-Jan-20", dataPath + "GOES16/timelapses/2026-Jan-20.avi", 30);
//     return 0;
// }