#ifndef TIMELAPSE_H
#define TIMELAPSE_H

#include <string>

// Returns true only if a video was successfully written, false otherwise
// (no decodable images, or the video writer could not be opened).
bool makeTimelapse(const std::string& folderPath, const std::string& outputFile, int fps = 30);

#endif