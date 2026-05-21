#ifndef LOGGER_H
#define LOGGER_H

#include <string>

void initLogger(const std::string& path);
void logInfo(const std::string& msg);
void logWarn(const std::string& msg);
void logError(const std::string& msg);

#endif
