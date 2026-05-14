#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

class HttpServer {
public:
    using JsonProvider = std::function<std::string()>;

    HttpServer(int port, JsonProvider provider);
    ~HttpServer();

    bool start();
    void stop();
    bool is_running() const;

private:
    int port_;
    JsonProvider provider_;

    int server_fd_;
    std::atomic<bool> running_;
    std::thread worker_;

    void run();

    static std::string build_http_response(
        const std::string& body,
        const std::string& content_type,
        const std::string& status_line
    );
};