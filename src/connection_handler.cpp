#include "connection_handler.h"
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

ConnectionHandler::ConnectionHandler(Router& router, Logger& logger)
    : router(router), logger(logger) {}

void ConnectionHandler::appendResponse(Connection& connection, const HttpRequest& request, HttpResponse response) {
    response.keepAlive = response.keepAlive && request.keepAlive;
    connection.writeBuffer += response.serialize(request.method == "HEAD");
    if (!response.keepAlive) {
        connection.closeAfterWrite = true;
    }

    logger.log(LogData(request.header("host"), LogLevel::INFO,
                       request.method + " " + request.target + " " + request.version,
                       std::to_string(response.status), std::to_string(response.body.size())));
}

ConnectionHandler::Action ConnectionHandler::onReadable(Connection& connection) {
    char buffer[16 * 1024];

    while (true) {
        const ssize_t bytesRead = ::recv(connection.fd, buffer, sizeof(buffer), 0);
        if (bytesRead > 0) {
            connection.readBuffer.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }
        if (bytesRead == 0) {
            connection.closeAfterWrite = true;  // peer sent FIN
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;  // socket drained
        }
        return Action::Close;
    }

    connection.lastActive = std::chrono::steady_clock::now();

    // A single read can contain several pipelined requests, or only part of
    // one. Keep parsing until the buffer holds nothing complete.
    while (!connection.readBuffer.empty()) {
        const RequestParser::Result result = connection.parser.parse(connection.readBuffer);
        if (result == RequestParser::Result::NeedMore) {
            break;
        }
        if (result == RequestParser::Result::Error) {
            HttpRequest bad = connection.parser.request();
            bad.keepAlive = false;
            appendResponse(connection, bad, HttpResponse::error(connection.parser.errorStatus()));
            connection.closeAfterWrite = true;
            connection.readBuffer.clear();
            break;
        }

        const HttpRequest& request = connection.parser.request();
        HttpResponse response = router.route(request);
        appendResponse(connection, request, response);
        const bool keepGoing = !connection.closeAfterWrite;
        connection.parser.reset();
        if (!keepGoing) {
            connection.readBuffer.clear();
            break;
        }
    }

    return flush(connection);
}

ConnectionHandler::Action ConnectionHandler::onWritable(Connection& connection) {
    return flush(connection);
}

ConnectionHandler::Action ConnectionHandler::flush(Connection& connection) {
    while (connection.writeOffset < connection.writeBuffer.size()) {
        const ssize_t written = ::send(connection.fd,
                                       connection.writeBuffer.data() + connection.writeOffset,
                                       connection.writeBuffer.size() - connection.writeOffset,
                                       MSG_NOSIGNAL);
        if (written > 0) {
            connection.writeOffset += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // Kernel buffer is full: finish this response on EPOLLOUT.
            return Action::WaitWrite;
        }
        return Action::Close;
    }

    connection.writeBuffer.clear();
    connection.writeOffset = 0;
    return connection.closeAfterWrite ? Action::Close : Action::WaitRead;
}
