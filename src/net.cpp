#include "net.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int createListener(const std::string& ip, int port, bool reusePort, std::string& errorOut) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        errorOut = std::string("socket(): ") + std::strerror(errno);
        return -1;
    }

    int enable = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    if (reusePort) {
        if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable)) < 0) {
            errorOut = std::string("SO_REUSEPORT: ") + std::strerror(errno);
            ::close(fd);
            return -1;
        }
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1) {
        errorOut = "invalid bind address: " + ip;
        ::close(fd);
        return -1;
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        errorOut = std::string("bind(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        errorOut = std::string("listen(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (!setNonBlocking(fd)) {
        errorOut = std::string("fcntl(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}
