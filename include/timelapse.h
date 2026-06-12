#ifndef TIMELAPSE_H
#define TIMELAPSE_H

#include <string>

// Returns true only if a video was successfully written, false otherwise
// (no decodable images, or the video writer could not be opened).
// format: "AVI", "MP4", "MKV", "WEBM", "H264"
bool makeTimelapse(const std::string& folderPath, const std::string& outputFile, int fps = 30, const std::string& format = "MP4");

// Returns the file extension (with dot) for a given format string.
std::string formatExtension(const std::string& format);

#endif