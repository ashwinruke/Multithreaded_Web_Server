#include "server.h"
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

namespace {
// send() can write fewer bytes than asked for, so loop until the whole
// response is out or the peer goes away.
bool sendAll(int socket, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t written = ::send(socket, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (written <= 0) {
            if (written < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        sent += static_cast<size_t>(written);
    }
    return true;
}
}  // namespace

Server::Server(const std::string& serverIP, int serverPort, int maxThreads, int cacheCapacity)
    : serverIP(serverIP), serverPort(serverPort), maxThreads(maxThreads),
      http_response(cacheCapacity), logger("server.log") {
    pthread_mutex_init(&mutex, nullptr);
    pthread_cond_init(&condition, nullptr);
}

Server::~Server() {
    stop();
}

void Server::start() {
    if (isRunning.exchange(true)) {
        logger.log(LogLevel::ERR, "Server is already running");
        return;
    }

    if (!initSocket()) {
        isRunning = false;
        return;
    }
    initThreadPool();
    logger.log(LogLevel::INFO, "Server running on " + serverIP + ":" + std::to_string(serverPort));

    while (isRunning) {
        int clientSocket = acceptClientConnection();
        if (clientSocket >= 0) {
            enqueueClientRequest(clientSocket);
        }
    }
}

bool Server::initSocket() {
    serverSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        logger.log(LogLevel::ERR, std::string("Failed to create server socket: ") + std::strerror(errno));
        return false;
    }

    // Without SO_REUSEADDR the port stays in TIME_WAIT for ~60s after a restart.
    int reuse = 1;
    ::setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<uint16_t>(serverPort));
    if (::inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) != 1) {
        logger.log(LogLevel::ERR, "Invalid server IP: " + serverIP);
        return false;
    }

    if (::bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0) {
        logger.log(LogLevel::ERR, std::string("Failed to bind port ") + std::to_string(serverPort) + ": " + std::strerror(errno));
        return false;
    }

    if (::listen(serverSocket, SOMAXCONN) < 0) {
        logger.log(LogLevel::ERR, std::string("Failed to listen: ") + std::strerror(errno));
        return false;
    }
    return true;
}

void Server::initThreadPool() {
    // Fixed-size pool: all workers start up front and block on the condition
    // variable until work arrives.
    for (int i = 0; i < maxThreads; ++i) {
        pthread_t thread;
        if (pthread_create(&thread, nullptr, workerThreadRoutine, this) == 0) {
            workerThreads.push_back(thread);
        }
    }
}

void* Server::workerThreadRoutine(void* serverPtr) {
    Server* server = static_cast<Server*>(serverPtr);
    while (true) {
        int clientSocket = server->dequeueClientRequest();
        if (clientSocket < 0) {
            break;  // shutting down
        }
        server->processClientRequest(clientSocket);
        ::close(clientSocket);
    }
    return nullptr;
}

int Server::acceptClientConnection() {
    int clientSocket = ::accept(serverSocket, nullptr, nullptr);
    if (clientSocket < 0 && isRunning && errno != EINTR && errno != EBADF) {
        logger.log(LogLevel::ERR, std::string("accept() failed: ") + std::strerror(errno));
    }
    return clientSocket;
}

void Server::enqueueClientRequest(int clientSocket) {
    pthread_mutex_lock(&mutex);
    clientQueue.push(clientSocket);
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
}

int Server::dequeueClientRequest() {
    pthread_mutex_lock(&mutex);
    while (clientQueue.empty() && isRunning) {
        pthread_cond_wait(&condition, &mutex);
    }
    int clientSocket = -1;
    if (!clientQueue.empty()) {
        clientSocket = clientQueue.front();
        clientQueue.pop();
    }
    pthread_mutex_unlock(&mutex);
    return clientSocket;
}

void Server::processClientRequest(int clientSocket) {
    constexpr size_t bufferSize = 8192;
    std::vector<char> buffer(bufferSize);
    ssize_t bytesRead = ::recv(clientSocket, buffer.data(), bufferSize, 0);
    if (bytesRead <= 0) {
        return;
    }

    std::string request(buffer.data(), static_cast<size_t>(bytesRead));
    std::string response = http_response.makeResponse(request);
    sendAll(clientSocket, response);

    logger.log(LogData(http_parser.getHeaderFieldVal(request, "Host"), LogLevel::INFO,
                       http_parser.getStartLine(request), http_parser.getResponseCode(response),
                       http_parser.getHeaderFieldVal(response, "Content-Length")));
}

void Server::stop() {
    if (!isRunning.exchange(false)) {
        return;
    }
    cleanup();
}

void Server::cleanup() {
    // Closing the listening socket unblocks accept() in start().
    if (serverSocket >= 0) {
        ::shutdown(serverSocket, SHUT_RDWR);
        ::close(serverSocket);
        serverSocket = -1;
    }

    // Wake every worker so it sees isRunning == false and returns.
    pthread_mutex_lock(&mutex);
    pthread_cond_broadcast(&condition);
    pthread_mutex_unlock(&mutex);

    for (pthread_t thread : workerThreads) {
        pthread_join(thread, nullptr);
    }
    workerThreads.clear();

    pthread_mutex_lock(&mutex);
    while (!clientQueue.empty()) {
        ::close(clientQueue.front());
        clientQueue.pop();
    }
    pthread_mutex_unlock(&mutex);

    logger.log(LogLevel::INFO, "Server stopped");
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&condition);
}
