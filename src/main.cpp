#include "http_server.hpp"
#include "media_index.hpp"
#include "scanner.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace {
std::atomic<bool> g_running{true};

void handle_signal(int) {
    g_running = false;
}

fs::path get_home_dir() {
    const char* home = std::getenv("HOME");
    if (home != nullptr && home[0] != '\0') {
        return fs::path(home);
    }
    return fs::current_path();
}

void print_usage(const char* program) {
    std::cout
        << "Usage: " << program << " [--path DIR] [--interval SEC] [--serve] [--port PORT] [--no-file]\n"
        << "  --path DIR       Root directory to scan (default: $HOME)\n"
        << "  --interval SEC   Scan interval in seconds (default: 60)\n"
        << "  --serve          Start HTTP server on /media_files\n"
        << "  --port PORT      HTTP server port (default: 1234)\n"
        << "  --no-file        Do not write ~/.media_files, serve JSON only\n";
}

int parse_positive_int(const std::string& value, int fallback) {
    try {
        size_t pos = 0;
        int parsed = std::stoi(value, &pos);
        if (pos != value.size() || parsed <= 0) {
            return fallback;
        }
        return parsed;
    } catch (...) {
        return fallback;
    }
}
} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    fs::path home_dir = get_home_dir();
    fs::path scan_root = home_dir;
    fs::path output_file = home_dir / ".media_files";

    int interval_seconds = 60;
    bool serve_http = false;
    bool write_file = true;
    int http_port = 1234;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--path" && i + 1 < argc) {
            scan_root = fs::path(argv[++i]);
        } else if (arg == "--interval" && i + 1 < argc) {
            interval_seconds = parse_positive_int(argv[++i], interval_seconds);
        } else if (arg == "--serve") {
            serve_http = true;
        } else if (arg == "--port" && i + 1 < argc) {
            http_port = parse_positive_int(argv[++i], http_port);
        } else if (arg == "--no-file") {
            write_file = false;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            print_usage(argv[0]);
            return 1;
        }
    }

    Scanner scanner(scan_root);
    MediaIndex index;

    scanner.full_scan(index);

    std::mutex json_mutex;
    std::string current_json = index.to_json();

    if (write_file) {
        std::error_code ec;
        fs::create_directories(output_file.parent_path(), ec);

        std::ofstream out(output_file, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << "Failed to write " << output_file << '\n';
        } else {
            out << current_json;
        }
    }

    HttpServer server(
        http_port,
        [&]() {
            std::lock_guard<std::mutex> lock(json_mutex);
            return current_json;
        }
    );

    if (serve_http) {
        if (!server.start()) {
            std::cerr << "Failed to start HTTP server on port " << http_port << '\n';
            return 1;
        }
        std::cout << "HTTP server started: http://localhost:" << http_port << "/media_files\n";
    }

    std::cout << "Scanning root: " << scan_root << '\n';
    if (write_file) {
        std::cout << "Output file: " << output_file << '\n';
    } else {
        std::cout << "Output file: disabled (--no-file)\n";
    }
    std::cout << "Interval: " << interval_seconds << " sec\n";

    while (g_running) {
        scanner.incremental_scan(index);

        const std::string json = index.to_json();
        {
            std::lock_guard<std::mutex> lock(json_mutex);
            current_json = json;
        }

        if (write_file) {
            std::ofstream out(output_file, std::ios::binary);
            if (!out.is_open()) {
                std::cerr << "Failed to write " << output_file << '\n';
            } else {
                out << json;
            }
        }

        std::cout << "Media catalog updated\n";

        for (int i = 0; i < interval_seconds && g_running; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    if (serve_http) {
        server.stop();
    }

    std::cout << "Stopped\n";
    return 0;
}