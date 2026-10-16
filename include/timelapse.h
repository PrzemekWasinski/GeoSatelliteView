#ifndef TIMELAPSE_H
#define TIMELAPSE_H

#include <string>
#include <vector>

// Returns true only if a video was successfully written, false otherwise
// (no decodable images, or the video writer could not be opened).
// format: "AVI", "MP4", "MKV", "WEBM", "H264"
bool makeTimelapse(const std::string& folderPath, const std::string& outputFile, int fps = 30, const std::string& format = "MP4");

bool makeTimelapse(const std::vector<std::string>& imageFiles, const std::string& outputFile, int fps, const std::string& format);

// Returns the file extension (with dot) for a given format string.
std::string formatExtension(const std::string& format);

#endif