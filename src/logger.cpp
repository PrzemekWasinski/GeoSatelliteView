#include "../include/logger.h"
#include <fstream>
#include <mutex>
#include <ctime>
#include <iostream>

static std::ofstream logFile;
static std::mutex    logMtx;

static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void initLogger(const std::string& path) {
    logFile.open(path, std::ios::app);
    if (!logFile.is_open())
        std::cerr << "Warning: could not open log file " << path << "\n";
}

static void write(const std::string& level, const std::string& msg) {
    std::string line = timestamp() + "  " + level + "  " + msg;
    std::lock_guard<std::mutex> lock(logMtx);
    if (logFile.is_open()) {
        logFile << line << "\n";
        logFile.flush();
    }
    if (level == "ERROR") std::cerr << line << "\n";
    else                  std::cout << line << "\n";
}

void logInfo(const std::string& msg)  { write("INFO ", msg); }
void logWarn(const std::string& msg)  { write("WARN ", msg); }
void logError(const std::string& msg) { write("ERROR", msg); }
