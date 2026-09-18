#pragma once
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERR
};

struct LogData {
    std::string ip;
    LogLevel level;
    std::string startLine;
    std::string responseCode;
    std::string responseSize;

    LogData(const std::string& ip, LogLevel level, const std::string& startLine,
            const std::string& responseCode, const std::string& responseSize)
        : ip(ip), level(level), startLine(startLine), responseCode(responseCode), responseSize(responseSize) {}
};

// Asynchronous logger: callers push a formatted line onto a queue and return
// immediately, so no request thread ever blocks on disk I/O.
class Logger {
public:
    explicit Logger(const std::string& logFile);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(LogLevel level, const std::string& message);
    void log(const LogData& logData);

private:
    std::string filePath;
    std::ofstream logFileStream;
    std::queue<std::string> logQueue;
    std::mutex logMutex;
    std::condition_variable logCondition;
    std::atomic<bool> running{false};
    std::thread logThread;

    void drainLoop();
    void enqueue(std::string logLine);
    static std::string getTime();
    static std::string getLogLevelStr(LogLevel level);
};
