#include "event_loop.h"
#include "handlers.h"
#include "logger.h"
#include "router.h"
#include "utils.h"
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>

namespace {
std::atomic<EventLoop*> g_loop{nullptr};

void handleSignal(int) {
    EventLoop* loop = g_loop.load();
    if (loop != nullptr) {
        loop->stop();
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        auto env = parseEnvFile(projectRoot() + "/.env");
        ServerConfig config;
        if (env.count("SERVER_IP"))     config.ip = env["SERVER_IP"];
        if (env.count("SERVER_PORT"))   config.port = envStrToInt(env["SERVER_PORT"]);
        if (env.count("MAX_THREADS"))   config.threads = envStrToInt(env["MAX_THREADS"]);
        if (env.count("CACHE_CAPACITY")) config.cacheCapacity = envStrToInt(env["CACHE_CAPACITY"]);
        if (env.count("IDLE_TIMEOUT"))  config.idleTimeoutSeconds = envStrToInt(env["IDLE_TIMEOUT"]);
        if (env.count("LOOP_MODE"))     config.mode = env["LOOP_MODE"];

        // CLI overrides make benchmark sweeps scriptable:
        //   ./multithreaded-server --mode reactor --threads 8 --port 8081
        for (int i = 1; i + 1 < argc; i += 2) {
            const std::string flag = argv[i];
            const std::string value = argv[i + 1];
            if (flag == "--mode")         config.mode = value;
            else if (flag == "--threads") config.threads = envStrToInt(value);
            else if (flag == "--port")    config.port = envStrToInt(value);
            else if (flag == "--ip")      config.ip = value;
        }
        if (config.threads < 1) {
            config.threads = 1;
        }

        Logger logger("server.log");

        StaticFileHandler staticFiles(projectRoot() + "/static", config.cacheCapacity);
        Router router;
        router.setFallback([&staticFiles](const HttpRequest& request) { return staticFiles(request); });
        router.add("GET", "/healthz", [](const HttpRequest&) {
            return HttpResponse::json(R"({"status":"ok"})");
        });

        std::unique_ptr<EventLoop> loop;
        if (config.mode == "reactor") {
            loop = std::make_unique<ReactorLoop>(config, router, logger);
        }
        else {
            config.mode = "pool";
            loop = std::make_unique<PoolLoop>(config, router, logger);
        }

        g_loop = loop.get();
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        std::signal(SIGPIPE, SIG_IGN);

        std::cout << "mode=" << config.mode << " threads=" << config.threads
                  << "  http://" << config.ip << ":" << config.port << "\n";

        const bool ok = loop->run();
        g_loop = nullptr;
        std::cout << "shut down\n";
        return ok ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& err) {
        std::cerr << "Fatal: " << err.what() << "\n";
        return EXIT_FAILURE;
    }
}
