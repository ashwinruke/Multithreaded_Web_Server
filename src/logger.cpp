#include "logger.h"
#include "utils.h"
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

Logger::Logger(const std::string& logFile)
    : filePath(projectRoot() + "/logs/" + logFile) {
    std::error_code errorCode;
    std::filesystem::create_directories(std::filesystem::path(filePath).parent_path(), errorCode);

    logFileStream.open(filePath, std::ios::app);
    if (!logFileStream.is_open()) {
        std::cerr << "Failed to open log file at " << filePath << "\n";
        return;
    }
    running = true;
    logThread = std::thread(&Logger::drainLoop, this);
}

Logger::~Logger() {
    {
        std::lock_guard<std::mutex> lock(logMutex);
        running = false;
    }
    logCondition.notify_all();
    if (logThread.joinable()) {
        logThread.join();
    }
    if (logFileStream.is_open()) {
        logFileStream.close();
    }
}

void Logger::drainLoop() {
    while (true) {
        std::unique_lock<std::mutex> lock(logMutex);
        logCondition.wait(lock, [this] { return !logQueue.empty() || !running; });

        if (logQueue.empty() && !running) {
            return;  // stopped and nothing left to flush
        }

        std::string logLine = std::move(logQueue.front());
        logQueue.pop();
        lock.unlock();

        logFileStream << logLine;
        logFileStream.flush();
    }
}

void Logger::enqueue(std::string logLine) {
    if (!running) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logQueue.push(std::move(logLine));
    }
    logCondition.notify_one();
}

void Logger::log(LogLevel level, const std::string& message) {
    enqueue(getTime() + " " + getLogLevelStr(level) + " " + message + "\n");
}

void Logger::log(const LogData& logData) {
    enqueue(logData.ip + " - " + getTime() + " " + getLogLevelStr(logData.level) + " \"" +
            logData.startLine + "\" " + logData.responseCode + " " + logData.responseSize + "\n");
}

std::string Logger::getTime() {
    const std::time_t currentTime = std::time(nullptr);
    std::tm timeParts{};
    localtime_r(&currentTime, &timeParts);
    std::ostringstream timeStream;
    timeStream << std::put_time(&timeParts, "%Y-%m-%d %H:%M:%S");
    return timeStream.str();
}

std::string Logger::getLogLevelStr(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "[DEBUG]";
        case LogLevel::INFO:    return "[INFO]";
        case LogLevel::WARNING: return "[WARNING]";
        case LogLevel::ERR:     return "[ERROR]";
        default:                return "";
    }
}
