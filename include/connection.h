#pragma once
#include "http.h"
#include <chrono>
#include <string>

// One live TCP connection. Because reads are non-blocking and arrive in
// arbitrary chunks, every connection carries its own buffers and parser state
// between epoll wakeups.
struct Connection {
    int fd = -1;
    std::string readBuffer;
    std::string writeBuffer;
    size_t writeOffset = 0;
    RequestParser parser;
    bool closeAfterWrite = false;
    bool busy = false;  // a worker owns this connection (pool mode only)
    std::chrono::steady_clock::time_point lastActive = std::chrono::steady_clock::now();

    explicit Connection(int fd) : fd(fd) {}
};
