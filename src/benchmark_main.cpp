// benchmark_main.cpp — High-performance benchmarking tool for mini-redis.
//
// Milestone 9:
// Measures throughput (operations / second) and latency percentiles (min, avg, p50, p95, p99, max)
// across core Redis operations:
//   - PING
//   - SET
//   - GET
//   - LPUSH
//   - LPOP
//
// Supports customizable concurrency (-c), total operations (-n), pipeline depth (-P),
// host (-h), and port (-p).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "resp.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BenchmarkOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 6379;
    int total_requests = 10000;
    int num_clients = 10;
    int pipeline_depth = 1;
    std::string tests = "PING,SET,GET,LPUSH,LPOP";
};

struct BenchmarkResult {
    std::string test_name;
    int total_requests = 0;
    double elapsed_sec = 0.0;
    double ops_per_sec = 0.0;
    double min_ms = 0.0;
    double avg_ms = 0.0;
    double p50_ms = 0.0;
    double p95_ms = 0.0;
    double p99_ms = 0.0;
    double max_ms = 0.0;
};

SOCKET connect_client(const std::string& host, uint16_t port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    // Disable Nagle algorithm for accurate latency measurements
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&flag), sizeof(flag));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return INVALID_SOCKET;
    }
    return sock;
}

bool send_all(SOCKET sock, const std::string& data) {
    std::size_t total_sent = 0;
    while (total_sent < data.size()) {
        int sent = send(sock, data.data() + total_sent, static_cast<int>(data.size() - total_sent), 0);
        if (sent <= 0) {
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
    }
    return true;
}

bool read_responses(SOCKET sock, RespParser& parser, int expected_count) {
    int parsed_count = 0;
    char buffer[4096];

    while (parsed_count < expected_count) {
        RespValue val;
        while (parser.parse_next(val) == ParseStatus::Ok) {
            ++parsed_count;
            if (parsed_count == expected_count) {
                return true;
            }
        }

        int n = recv(sock, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return false;
        }
        parser.append(buffer, n);
    }
    return true;
}

BenchmarkResult run_single_benchmark(
    const std::string& test_name,
    const BenchmarkOptions& opts,
    const std::vector<std::string>& commands_payload
) {
    BenchmarkResult res;
    res.test_name = test_name;
    res.total_requests = opts.total_requests;

    int requests_per_client = opts.total_requests / opts.num_clients;
    std::vector<std::thread> workers;
    std::vector<std::vector<double>> all_latencies(opts.num_clients);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int c = 0; c < opts.num_clients; ++c) {
        workers.emplace_back([&, c]() {
            SOCKET sock = connect_client(opts.host, opts.port);
            if (sock == INVALID_SOCKET) {
                return;
            }

            RespParser parser;
            std::vector<double>& latencies = all_latencies[c];
            latencies.reserve(requests_per_client);

            int done = 0;
            int pipe = std::max(1, opts.pipeline_depth);

            while (done < requests_per_client) {
                int batch = std::min(pipe, requests_per_client - done);
                std::string batch_data;

                for (int b = 0; b < batch; ++b) {
                    int idx = (done + b) % static_cast<int>(commands_payload.size());
                    batch_data += commands_payload[idx];
                }

                auto t0 = std::chrono::high_resolution_clock::now();
                if (!send_all(sock, batch_data)) {
                    break;
                }
                if (!read_responses(sock, parser, batch)) {
                    break;
                }
                auto t1 = std::chrono::high_resolution_clock::now();

                double duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
                double per_op_ms = duration_ms / batch;
                for (int b = 0; b < batch; ++b) {
                    latencies.push_back(per_op_ms);
                }
                done += batch;
            }

            closesocket(sock);
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    res.elapsed_sec = std::chrono::duration<double>(end_time - start_time).count();

    // Aggregate latencies
    std::vector<double> combined;
    for (const auto& l : all_latencies) {
        combined.insert(combined.end(), l.begin(), l.end());
    }

    if (!combined.empty()) {
        std::sort(combined.begin(), combined.end());
        res.min_ms = combined.front();
        res.max_ms = combined.back();
        double sum = std::accumulate(combined.begin(), combined.end(), 0.0);
        res.avg_ms = sum / combined.size();
        res.p50_ms = combined[combined.size() * 50 / 100];
        res.p95_ms = combined[combined.size() * 95 / 100];
        res.p99_ms = combined[combined.size() * 99 / 100];
        res.ops_per_sec = combined.size() / (res.elapsed_sec > 0.0 ? res.elapsed_sec : 0.001);
    }

    return res;
}

void print_result(const BenchmarkResult& r, const BenchmarkOptions& opts) {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "  " << r.test_name << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "  " << r.total_requests << " requests completed in "
              << std::fixed << std::setprecision(3) << r.elapsed_sec << " seconds" << std::endl;
    std::cout << "  " << opts.num_clients << " parallel clients, pipeline depth: "
              << opts.pipeline_depth << std::endl;
    std::cout << "  Throughput: " << std::fixed << std::setprecision(2)
              << r.ops_per_sec << " requests/sec" << std::endl;
    std::cout << "\n  Latency Distribution (milliseconds):" << std::endl;
    std::cout << "    Min:  " << std::setw(8) << std::fixed << std::setprecision(3) << r.min_ms << " ms" << std::endl;
    std::cout << "    Avg:  " << std::setw(8) << std::fixed << std::setprecision(3) << r.avg_ms << " ms" << std::endl;
    std::cout << "    p50:  " << std::setw(8) << std::fixed << std::setprecision(3) << r.p50_ms << " ms" << std::endl;
    std::cout << "    p95:  " << std::setw(8) << std::fixed << std::setprecision(3) << r.p95_ms << " ms" << std::endl;
    std::cout << "    p99:  " << std::setw(8) << std::fixed << std::setprecision(3) << r.p99_ms << " ms" << std::endl;
    std::cout << "    Max:  " << std::setw(8) << std::fixed << std::setprecision(3) << r.max_ms << " ms" << std::endl;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "  -h <host>      Server hostname or IP (default: 127.0.0.1)\n"
              << "  -p <port>      Server TCP port (default: 6379)\n"
              << "  -c <clients>   Number of concurrent clients (default: 10)\n"
              << "  -n <requests>  Total number of requests per test (default: 10000)\n"
              << "  -P <pipeline>  Pipeline depth per client (default: 1)\n"
              << "  -t <tests>     Comma-separated tests (default: PING,SET,GET,LPUSH,LPOP)\n"
              << "  --help         Show this help message\n";
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    BenchmarkOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" && i + 1 < argc) {
            opts.host = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            opts.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        } else if (arg == "-c" && i + 1 < argc) {
            opts.num_clients = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "-n" && i + 1 < argc) {
            opts.total_requests = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "-P" && i + 1 < argc) {
            opts.pipeline_depth = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "-t" && i + 1 < argc) {
            opts.tests = argv[++i];
        } else if (arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "Failed to initialize Winsock." << std::endl;
        return 1;
    }

    // Probe server connectivity first
    SOCKET test_sock = connect_client(opts.host, opts.port);
    if (test_sock == INVALID_SOCKET) {
        std::cerr << "Could not connect to Redis server at " << opts.host << ":" << opts.port << std::endl;
        WSACleanup();
        return 1;
    }
    closesocket(test_sock);

    std::cout << "mini-redis Benchmark Suite (Milestone 9)" << std::endl;
    std::cout << "Target: " << opts.host << ":" << opts.port
              << " | Total: " << opts.total_requests
              << " | Clients: " << opts.num_clients
              << " | Pipeline: " << opts.pipeline_depth << std::endl;

    std::vector<BenchmarkResult> all_results;

    // Helper command builders
    auto build_ping = []() {
        return RespValue::make_array({RespValue::make_bulk_string("PING")}).serialize();
    };

    auto build_set = [](int i) {
        return RespValue::make_array({
            RespValue::make_bulk_string("SET"),
            RespValue::make_bulk_string("bench:key:" + std::to_string(i)),
            RespValue::make_bulk_string("benchmark_value_" + std::to_string(i))
        }).serialize();
    };

    auto build_get = [](int i) {
        return RespValue::make_array({
            RespValue::make_bulk_string("GET"),
            RespValue::make_bulk_string("bench:key:" + std::to_string(i))
        }).serialize();
    };

    auto build_lpush = [](int i) {
        return RespValue::make_array({
            RespValue::make_bulk_string("LPUSH"),
            RespValue::make_bulk_string("bench:list"),
            RespValue::make_bulk_string("elem_" + std::to_string(i))
        }).serialize();
    };

    auto build_lpop = []() {
        return RespValue::make_array({
            RespValue::make_bulk_string("LPOP"),
            RespValue::make_bulk_string("bench:list")
        }).serialize();
    };

    // Check requested tests
    auto should_run = [&](const std::string& name) {
        return opts.tests.find(name) != std::string::npos;
    };

    if (should_run("PING")) {
        std::vector<std::string> cmds = {build_ping()};
        auto res = run_single_benchmark("PING", opts, cmds);
        print_result(res, opts);
        all_results.push_back(res);
    }

    if (should_run("SET")) {
        std::vector<std::string> cmds;
        cmds.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            cmds.push_back(build_set(i));
        }
        auto res = run_single_benchmark("SET", opts, cmds);
        print_result(res, opts);
        all_results.push_back(res);
    }

    if (should_run("GET")) {
        std::vector<std::string> cmds;
        cmds.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            cmds.push_back(build_get(i));
        }
        auto res = run_single_benchmark("GET", opts, cmds);
        print_result(res, opts);
        all_results.push_back(res);
    }

    if (should_run("LPUSH")) {
        std::vector<std::string> cmds;
        cmds.reserve(1000);
        for (int i = 0; i < 1000; ++i) {
            cmds.push_back(build_lpush(i));
        }
        auto res = run_single_benchmark("LPUSH", opts, cmds);
        print_result(res, opts);
        all_results.push_back(res);
    }

    if (should_run("LPOP")) {
        std::vector<std::string> cmds = {build_lpop()};
        auto res = run_single_benchmark("LPOP", opts, cmds);
        print_result(res, opts);
        all_results.push_back(res);
    }

    // Final Summary
    std::cout << "\n==========================================" << std::endl;
    std::cout << "  BENCHMARK SUMMARY" << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << std::left << std::setw(10) << "Command"
              << std::right << std::setw(16) << "Ops / Sec"
              << std::setw(14) << "Avg Latency"
              << std::setw(14) << "p99 Latency" << std::endl;
    std::cout << "------------------------------------------" << std::endl;
    for (const auto& r : all_results) {
        std::cout << std::left << std::setw(10) << r.test_name
                  << std::right << std::setw(16) << std::fixed << std::setprecision(2) << r.ops_per_sec
                  << std::setw(11) << std::fixed << std::setprecision(3) << r.avg_ms << " ms"
                  << std::setw(11) << std::fixed << std::setprecision(3) << r.p99_ms << " ms"
                  << std::endl;
    }
    std::cout << "==========================================\n" << std::endl;

    WSACleanup();
    return 0;
}
