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

ReactorLoop::ReactorLoop(const ServerConfig& config, Router& router, Logger& logger, Metrics& metrics)
    : config(config), logger(logger), metrics(metrics), handler(router, logger, metrics) {}

ReactorLoop::~ReactorLoop() {
    stop();
}

bool ReactorLoop::setupReactor(Reactor& reactor, std::string& errorOut) {
    reactor.listenFd = createListener(config.ip, config.port, true, errorOut);
    if (reactor.listenFd < 0) {
        return false;
    }

    reactor.epollFd = ::epoll_create1(0);
    reactor.stopFd = ::eventfd(0, EFD_NONBLOCK);
    if (reactor.epollFd < 0 || reactor.stopFd < 0) {
        errorOut = std::string("epoll/eventfd: ") + std::strerror(errno);
        return false;
    }

    epoll_event listenEvent{};
    listenEvent.events = EPOLLIN;
    listenEvent.data.fd = reactor.listenFd;
    ::epoll_ctl(reactor.epollFd, EPOLL_CTL_ADD, reactor.listenFd, &listenEvent);

    epoll_event stopEvent{};
    stopEvent.events = EPOLLIN;
    stopEvent.data.fd = reactor.stopFd;
    ::epoll_ctl(reactor.epollFd, EPOLL_CTL_ADD, reactor.stopFd, &stopEvent);
    return true;
}

bool ReactorLoop::run() {
    running = true;
    for (int i = 0; i < config.threads; ++i) {
        auto reactor = std::make_unique<Reactor>();
        std::string error;
        if (!setupReactor(*reactor, error)) {
            logger.log(LogLevel::ERR, "Reactor " + std::to_string(i) + " failed: " + error);
            running = false;
            return false;
        }
        reactors.push_back(std::move(reactor));
    }

    logger.log(LogLevel::INFO, "reactor mode: " + std::to_string(config.threads) +
                               " threads with SO_REUSEPORT on " + config.ip + ":" + std::to_string(config.port));

    for (auto& reactor : reactors) {
        threads.emplace_back(&ReactorLoop::reactorLoop, this, std::ref(*reactor));
    }
    for (std::thread& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    threads.clear();
    return true;
}

void ReactorLoop::reactorLoop(Reactor& reactor) {
    std::vector<epoll_event> events(kMaxEvents);
    auto lastSweep = std::chrono::steady_clock::now();

    while (running) {
        const int ready = ::epoll_wait(reactor.epollFd, events.data(), kMaxEvents, 1000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < ready; ++i) {
            const int fd = events[i].data.fd;

            if (fd == reactor.stopFd) {
                return;
            }

            if (fd == reactor.listenFd) {
                while (true) {
                    const int clientFd = ::accept(reactor.listenFd, nullptr, nullptr);
                    if (clientFd < 0) {
                        break;
                    }
                    setNonBlocking(clientFd);
                    int noDelay = 1;
                    ::setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

                    reactor.connections[clientFd] = std::make_shared<Connection>(clientFd);
                    metrics.connectionOpened();
                    epoll_event clientEvent{};
                    clientEvent.events = EPOLLIN;
                    clientEvent.data.fd = clientFd;
                    ::epoll_ctl(reactor.epollFd, EPOLL_CTL_ADD, clientFd, &clientEvent);
                }
                continue;
            }

            auto iterator = reactor.connections.find(fd);
            if (iterator == reactor.connections.end()) {
                continue;
            }
            Connection& connection = *iterator->second;

            ConnectionHandler::Action action;
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                action = ConnectionHandler::Action::Close;
            }
            else if (events[i].events & EPOLLOUT) {
                action = handler.onWritable(connection);
            }
            else {
                action = handler.onReadable(connection);
            }

            if (action == ConnectionHandler::Action::Close) {
                closeConnection(reactor, fd);
                continue;
            }

            epoll_event nextEvent{};
            nextEvent.events = (action == ConnectionHandler::Action::WaitWrite) ? EPOLLOUT : EPOLLIN;
            nextEvent.data.fd = fd;
            if (::epoll_ctl(reactor.epollFd, EPOLL_CTL_MOD, fd, &nextEvent) < 0) {
                closeConnection(reactor, fd);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - lastSweep >= std::chrono::seconds(1)) {
            sweepIdleConnections(reactor);
            lastSweep = now;
        }
    }
}

void ReactorLoop::closeConnection(Reactor& reactor, int fd) {
    if (reactor.connections.erase(fd) == 0) {
        return;
    }
    ::epoll_ctl(reactor.epollFd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    metrics.connectionClosed();
}

void ReactorLoop::sweepIdleConnections(Reactor& reactor) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> expired;
    for (const auto& [fd, connection] : reactor.connections) {
        if (now - connection->lastActive > std::chrono::seconds(config.idleTimeoutSeconds)) {
            expired.push_back(fd);
        }
    }
    for (int fd : expired) {
        closeConnection(reactor, fd);
    }
}

void ReactorLoop::stop() {
    if (!running.exchange(false)) {
        return;
    }
    for (auto& reactor : reactors) {
        if (reactor->stopFd >= 0) {
            const uint64_t value = 1;
            [[maybe_unused]] ssize_t ignored = ::write(reactor->stopFd, &value, sizeof(value));
        }
    }
}
