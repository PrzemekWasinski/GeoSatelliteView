#include <opencv2/opencv.hpp>
#include "timelapse.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>

void makeTimelapse(const std::string& folderPath, const std::string& outputFile, int fps) {
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
    
    //sort images alphabetically
    std::sort(imageFiles.begin(), imageFiles.end());
    
    if (imageFiles.empty()) {
        std::cerr << "No images found" << std::endl;
        return;
    }
    
    //load first image to get size
    cv::Mat firstFrame = cv::imread(imageFiles[0]);
    
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
                            cv::VideoWriter::fourcc('a','v','c','1'),  
                            fps,
                            frameSize);
    
    if (!writer.isOpened()) {
        std::cerr << "Failed to open video writer!" << std::endl;
        return;
    }
    
    //write frames
    for (const auto& file : imageFiles) {
        cv::Mat img = cv::imread(file);
        if (!img.empty()) {
            //resize if needed
            if (img.size() != frameSize) {
                cv::resize(img, img, frameSize);
            }
            writer.write(img);
            std::cout << "." << std::flush;  
        }
    }
    
    writer.release();
    std::cout << "\nTimelapse saved to " << outputFile << std::endl;
}

// int main() {
//     makeTimelapse("./data/GOES16", "timelapse.mp4", 24);
//     return 0;
// }