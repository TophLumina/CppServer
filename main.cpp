#include <atomic>
#include <chrono>
#include <sstream>
#include <string>

#include <httplib.h>

int main() {
    using Clock = std::chrono::steady_clock;
    const auto start_time = Clock::now();
    std::atomic<unsigned long long> request_count{0};

    httplib::Server server;

    server.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        request_count++;
        res.set_content("C++ status server is running. Try /status\n", "text/plain; charset=utf-8");
    });

    server.Get("/status", [&](const httplib::Request&, httplib::Response& res) {
        request_count++;

        const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - start_time).count();

        std::ostringstream body;
        body << "{";
        body << "\"ok\":true,";
        body << "\"uptime_seconds\":" << uptime_seconds << ",";
        body << "\"request_count\":" << request_count.load();
        body << "}";

        res.set_content(body.str(), "application/json; charset=utf-8");
    });

    server.Get("/request-status", [&](const httplib::Request& req, httplib::Response& res) {
        request_count++;

        const auto uptime_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            Clock::now() - start_time).count();

        std::ostringstream body;
        body << "{";
        body << "\"ok\":true,";
        body << "\"method\":\"" << req.method << "\",";
        body << "\"path\":\"" << req.path << "\",";
        body << "\"uptime_seconds\":" << uptime_seconds << ",";
        body << "\"request_count\":" << request_count.load();
        body << "}";

        res.set_content(body.str(), "application/json; charset=utf-8");
    });

    const char* host = "0.0.0.0";
    const int port = 8080;

    return server.listen(host, port) ? 0 : 1;
}
