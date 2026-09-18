#pragma once
#include "http_parser.h"
#include "http_response.h"
#include "logger.h"
#include <atomic>
#include <queue>
#include <string>
#include <vector>
#include <pthread.h>
#include <netinet/in.h>

class Server {
public:
    Server(const std::string& serverIP, int serverPort, int maxThreads, int cacheCapacity);
    ~Server();

    void start();
    void stop();

private:
    std::string serverIP;
    int serverPort;
    int maxThreads;
    std::atomic<bool> isRunning{false};
    int serverSocket = -1;
    sockaddr_in serverAddr{};

    std::queue<int> clientQueue;
    std::vector<pthread_t> workerThreads;
    pthread_mutex_t mutex;
    pthread_cond_t condition;

    HttpParser http_parser;
    HttpResponse http_response;
    Logger logger;

    bool initSocket();
    void initThreadPool();
    static void* workerThreadRoutine(void* serverPtr);
    int acceptClientConnection();
    void enqueueClientRequest(int clientSocket);
    int dequeueClientRequest();
    void processClientRequest(int clientSocket);
    void cleanup();
};
