#pragma once
#include "connection_handler.h"
#include "logger.h"
#include "metrics.h"
#include "router.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct ServerConfig {
    std::string ip = "127.0.0.1";
    int port = 8080;
    int threads = 4;
    int cacheCapacity = 64;
    int idleTimeoutSeconds = 15;
    std::string mode = "pool";  // "pool" or "reactor"
};

class EventLoop {
public:
    virtual ~EventLoop() = default;
    virtual bool run() = 0;   // blocks until stop()
    virtual void stop() = 0;
};

// Model A: one epoll thread owns accept() and readiness notification; a fixed
// worker pool does the parsing, handler work and socket I/O. EPOLLONESHOT
// guarantees only one worker owns a connection at any moment.
class PoolLoop : public EventLoop {
public:
    PoolLoop(const ServerConfig& config, Router& router, Logger& logger, Metrics& metrics);
    ~PoolLoop() override;

    bool run() override;
    void stop() override;

private:
    struct Event {
        int fd;
        uint32_t events;
    };

    ServerConfig config;
    Logger& logger;
    Metrics& metrics;
    ConnectionHandler handler;

    int epollFd = -1;
    int listenFd = -1;
    int stopFd = -1;
    std::atomic<bool> running{false};

    std::unordered_map<int, std::shared_ptr<Connection>> connections;
    std::mutex connectionsMutex;

    std::queue<Event> workQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::vector<std::thread> workers;

    void acceptLoop();
    void workerLoop();
    void handleEvent(const Event& event);
    void rearm(int fd, uint32_t events);
    void closeConnection(int fd);
    void sweepIdleConnections();
};

// Model B: N independent reactors. Each thread has its own epoll instance and
// its own listening socket via SO_REUSEPORT, so the kernel spreads incoming
// connections and no lock is needed on the data path.
class ReactorLoop : public EventLoop {
public:
    ReactorLoop(const ServerConfig& config, Router& router, Logger& logger, Metrics& metrics);
    ~ReactorLoop() override;

    bool run() override;
    void stop() override;

private:
    struct Reactor {
        int epollFd = -1;
        int listenFd = -1;
        int stopFd = -1;
        std::unordered_map<int, std::shared_ptr<Connection>> connections;
    };

    ServerConfig config;
    Logger& logger;
    Metrics& metrics;
    ConnectionHandler handler;
    std::atomic<bool> running{false};
    std::vector<std::unique_ptr<Reactor>> reactors;
    std::vector<std::thread> threads;

    bool setupReactor(Reactor& reactor, std::string& errorOut);
    void reactorLoop(Reactor& reactor);
    void closeConnection(Reactor& reactor, int fd);
    void sweepIdleConnections(Reactor& reactor);
};
