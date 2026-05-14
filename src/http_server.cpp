#include "http_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

HttpServer::HttpServer(int port, JsonProvider provider)
    : port_(port),
      provider_(std::move(provider)),
      server_fd_(-1),
      running_(false) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (running_) {
        return true;
    }

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return false;
    }

    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 16) < 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    worker_ = std::thread(&HttpServer::run, this);
    return true;
}

void HttpServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
}

bool HttpServer::is_running() const {
    return running_;
}

std::string HttpServer::build_http_response(
    const std::string& body,
    const std::string& content_type,
    const std::string& status_line
) {
    std::string response;
    response += status_line;
    response += "\r\n";
    response += "Content-Type: ";
    response += content_type;
    response += "\r\n";
    response += "Content-Length: ";
    response += std::to_string(body.size());
    response += "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    return response;
}

void HttpServer::run() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (running_) {
                continue;
            }
            break;
        }

        char buffer[4096];
        const ssize_t received = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            ::close(client_fd);
            continue;
        }

        buffer[received] = '\0';
        std::string request(buffer);

        const bool is_get_media =
            request.rfind("GET /media_files ", 0) == 0 ||
            request.rfind("GET /media_files?", 0) == 0;

        std::string response;
        if (is_get_media) {
            const std::string body = provider_ ? provider_() : "{}";
            response = build_http_response(
                body,
                "application/json; charset=utf-8",
                "HTTP/1.1 200 OK"
            );
        } else {
            const std::string body = "Not Found\n";
            response = build_http_response(
                body,
                "text/plain; charset=utf-8",
                "HTTP/1.1 404 Not Found"
            );
        }

        (void)::send(client_fd, response.c_str(), response.size(), 0);
        ::close(client_fd);
    }
}