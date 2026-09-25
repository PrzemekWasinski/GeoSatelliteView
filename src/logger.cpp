#include "../include/logger.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <system_error>
#include <mutex>
#include <ctime>
#include <iostream>

static std::mutex    logMtx;

static std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void initLogger(const std::string& path) {
    // Capture application messages and native OpenCV/codec diagnostics alike.
    std::cout.flush();
    std::cerr.flush();
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        throw std::system_error(errno, std::generic_category(), "Cannot open log " + path);
    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        const int error = errno;
        if (fd > STDERR_FILENO) close(fd);
        throw std::system_error(error, std::generic_category(), "Cannot redirect log " + path);
    }
    if (fd > STDERR_FILENO) close(fd);
    std::cout << std::unitbuf;
}

static void write(const std::string& level, const std::string& msg) {
    std::string line = timestamp() + "  " + level + "  " + msg;
    std::lock_guard<std::mutex> lock(logMtx);
    if (level == "ERROR") std::cerr << line << "\n";
    else                  std::cout << line << "\n";
}

void logInfo(const std::string& msg)  { write("INFO ", msg); }
void logWarn(const std::string& msg)  { write("WARN ", msg); }
void logError(const std::string& msg) { write("ERROR", msg); }
