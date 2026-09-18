#include "server.h"
#include "http_parser.h"
#include "utils.h"
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
std::unique_ptr<Server> g_server;

void handleSignal(int) {
    // Only async-signal-safe work belongs here; stop() is called from main.
    if (g_server) {
        g_server->stop();
    }
}
}  // namespace

int main() {
    try {
        auto env = parseEnvFile(projectRoot() + "/.env");
        const std::string serverIP = env.count("SERVER_IP") ? env["SERVER_IP"] : "127.0.0.1";
        const int serverPort = env.count("SERVER_PORT") ? envStrToInt(env["SERVER_PORT"]) : 8080;
        const int maxThreads = env.count("MAX_THREADS") ? envStrToInt(env["MAX_THREADS"]) : 8;
        const int cacheCapacity = env.count("CACHE_CAPACITY") ? envStrToInt(env["CACHE_CAPACITY"]) : 64;

        HttpParser::setStaticRoot(projectRoot() + "/static");

        g_server = std::make_unique<Server>(serverIP, serverPort, maxThreads, cacheCapacity);

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        std::signal(SIGPIPE, SIG_IGN);

        std::cout << "Listening on http://" << serverIP << ":" << serverPort << "\n";
        g_server->start();
        g_server.reset();
    }
    catch (const std::exception& err) {
        std::cerr << "Fatal: " << err.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
