#pragma once
#include "connection.h"
#include "logger.h"
#include "metrics.h"
#include "router.h"

// Protocol logic shared by both event loops: drain the socket, parse whatever
// arrived, run handlers, queue the response. Keeping this out of the loops is
// what lets the two concurrency models be compared on equal terms.
class ConnectionHandler {
public:
    enum class Action { WaitRead, WaitWrite, Close };

    ConnectionHandler(Router& router, Logger& logger, Metrics& metrics);

    Action onReadable(Connection& connection);
    Action onWritable(Connection& connection);

private:
    Router& router;
    Logger& logger;
    Metrics& metrics;

    Action flush(Connection& connection);
    void appendResponse(Connection& connection, const HttpRequest& request, HttpResponse response);
};
