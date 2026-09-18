#include "event_loop.h"
#include "net.h"
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int kMaxEvents = 256;
}

PoolLoop::PoolLoop(const ServerConfig& config, Router& router, Logger& logger)
    : config(config), logger(logger), handler(router, logger) {}

PoolLoop::~PoolLoop() {
    stop();
}

bool PoolLoop::run() {
    std::string error;
    listenFd = createListener(config.ip, config.port, false, error);
    if (listenFd < 0) {
        logger.log(LogLevel::ERR, "Listener failed: " + error);
        return false;
    }

    epollFd = ::epoll_create1(0);
    stopFd = ::eventfd(0, EFD_NONBLOCK);
    if (epollFd < 0 || stopFd < 0) {
        logger.log(LogLevel::ERR, std::string("epoll/eventfd failed: ") + std::strerror(errno));
        return false;
    }

    epoll_event listenEvent{};
    listenEvent.events = EPOLLIN;
    listenEvent.data.fd = listenFd;
    ::epoll_ctl(epollFd, EPOLL_CTL_ADD, listenFd, &listenEvent);

    epoll_event stopEvent{};
    stopEvent.events = EPOLLIN;
    stopEvent.data.fd = stopFd;
    ::epoll_ctl(epollFd, EPOLL_CTL_ADD, stopFd, &stopEvent);

    running = true;
    for (int i = 0; i < config.threads; ++i) {
        workers.emplace_back(&PoolLoop::workerLoop, this);
    }

    logger.log(LogLevel::INFO, "pool mode: 1 epoll thread + " + std::to_string(config.threads) + " workers on " +
                               config.ip + ":" + std::to_string(config.port));
    acceptLoop();

    queueCondition.notify_all();
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers.clear();
    return true;
}

void PoolLoop::acceptLoop() {
    std::vector<epoll_event> events(kMaxEvents);
    auto lastSweep = std::chrono::steady_clock::now();

    while (running) {
        const int ready = ::epoll_wait(epollFd, events.data(), kMaxEvents, 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd;

            if (fd == stopFd) {
                running = false;
                break;
            }

            if (fd == listenFd) {
                // Level-triggered, but drain anyway: one wakeup can cover many
                // pending connections under load.
                while (true) {
                    const int clientFd = ::accept(listenFd, nullptr, nullptr);
                    if (clientFd < 0) {
                        break;
                    }
                    setNonBlocking(clientFd);
                    int noDelay = 1;
                    ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

                    {
                        std::lock_guard<std::mutex> lock(connectionsMutex);
                        connections[clientFd] = std::make_shared<Connection>(clientFd);
                    }

                    epoll_event clientEvent{};
                    clientEvent.events = EPOLLIN | EPOLLONESHOT;
                    clientEvent.data.fd = clientFd;
                    ::epoll_ctl(epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent);
                }
                continue;
            }

            // EPOLLONESHOT already disarmed this fd, so exactly one worker can
            // own the connection until it is rearmed.
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                workQueue.push(Event{fd, events[i].events});
            }
            queueCondition.notify_one();
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastSweep >= std::chrono::seconds(1)) {
            sweepIdleConnections();
            lastSweep = now;
        }
    }
}

void PoolLoop::workerLoop() {
    while (true) {
        Event event{-1, 0};
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCondition.wait(lock, [this] { return !workQueue.empty() || !running; });
            if (workQueue.empty()) {
                return;
            }
            event = workQueue.front();
            workQueue.pop();
        }
        handleEvent(event);
    }
}

void PoolLoop::handleEvent(const Event& event) {
    std::shared_ptr<Connection> connection;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        auto iterator = connections.find(event.fd);
        if (iterator == connections.end()) {
            return;
        }
        connection = iterator->second;
        connection->busy = true;
    }

    ConnectionHandler::Action action;
    if (event.events & (EPOLLHUP | EPOLLERR)) {
        action = ConnectionHandler::Action::Close;
    }
    else if (event.events & EPOLLOUT) {
        action = handler.onWritable(*connection);
    }
    else {
        action = handler.onReadable(*connection);
    }

    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        connection->busy = false;
    }

    switch (action) {
        case ConnectionHandler::Action::WaitRead:
            rearm(event.fd, EPOLLIN);
            break;
        case ConnectionHandler::Action::WaitWrite:
            rearm(event.fd, EPOLLOUT);
            break;
        case ConnectionHandler::Action::Close:
            closeConnection(event.fd);
            break;
    }
}

void PoolLoop::rearm(int fd, uint32_t events) {
    epoll_event event{};
    event.events = events | EPOLLONESHOT;
    event.data.fd = fd;
    if (::epoll_ctl(epollFd, EPOLL_CTL_MOD, fd, &event) < 0) {
        closeConnection(fd);
    }
}

void PoolLoop::closeConnection(int fd) {
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        if (connections.erase(fd) == 0) {
            return;
        }
    }
    ::epoll_ctl(epollFd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
}

void PoolLoop::sweepIdleConnections() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> expired;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        for (const auto& [fd, connection] : connections) {
            if (connection->busy) {
                continue;  // a worker owns it; let the worker decide
            }
            if (now - connection->lastActive > std::chrono::seconds(config.idleTimeoutSeconds)) {
                expired.push_back(fd);
            }
        }
    }
    for (int fd : expired) {
        closeConnection(fd);
    }
}

void PoolLoop::stop() {
    if (!running.exchange(false)) {
        return;
    }
    if (stopFd >= 0) {
        const uint64_t value = 1;
        [[maybe_unused]] ssize_t ignored = ::write(stopFd, &value, sizeof(value));
    }
    queueCondition.notify_all();

    std::vector<int> open;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        for (const auto& [fd, connection] : connections) {
            open.push_back(fd);
        }
        connections.clear();
    }
    for (int fd : open) {
        ::close(fd);
    }
    if (listenFd >= 0) {
        ::close(listenFd);
        listenFd = -1;
    }
}
