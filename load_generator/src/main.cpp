// main.cpp (load generator)
// Entry point for the HS-FinancialServices load generator.
//
// Usage:
//   hs_loadgen [--host <host>] [--port <port>] [--clients <n>]
//              [--duration <secs>] [--threads <n>]
//              [--sql-weight <n>] [--buy-weight <n>] [--sell-weight <n>]
//              [--position-weight <n>] [--history-weight <n>]

#include "client_session.hpp"
#include "metrics_collector.hpp"

#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <csignal>

namespace po  = boost::program_options;
namespace asio = boost::asio;

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    try {
        std::string host;
        uint16_t    port{9000};
        unsigned    num_clients{100};
        unsigned    duration_secs{10};
        unsigned    num_threads{0};

        hs::loadgen::ScenarioConfig scenario;

        po::options_description desc("HS-FinancialServices load generator");
        desc.add_options()
            ("help,h",     "Show help")
            ("host",       po::value<std::string>(&host)->default_value("127.0.0.1"), "Server host")
            ("port,p",     po::value<uint16_t>(&port)->default_value(9000),           "Server port")
            ("clients,c",  po::value<unsigned>(&num_clients)->default_value(100),     "Concurrent clients")
            ("duration,d", po::value<unsigned>(&duration_secs)->default_value(10),    "Test duration (seconds)")
            ("threads,t",  po::value<unsigned>(&num_threads)->default_value(0),       "I/O threads (0 = hardware_concurrency)")
            ("sql-weight",      po::value<int>(&scenario.weight_sql)->default_value(20),           "SQL request weight")
            ("buy-weight",      po::value<int>(&scenario.weight_buy)->default_value(25),           "Buy order weight")
            ("sell-weight",     po::value<int>(&scenario.weight_sell)->default_value(25),          "Sell order weight")
            ("position-weight", po::value<int>(&scenario.weight_position)->default_value(20),      "GetPosition weight")
            ("history-weight",  po::value<int>(&scenario.weight_price_history)->default_value(10), "GetPriceHistory weight");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << '\n';
            return 0;
        }

        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 1;
        }

        std::cout << "Starting load generator:\n"
                  << "  Target  : " << host << ':' << port << '\n'
                  << "  Clients : " << num_clients   << '\n'
                  << "  Duration: " << duration_secs << " s\n"
                  << "  Threads : " << num_threads   << '\n';

        asio::io_context ioc;

        hs::loadgen::MetricsCollector metrics;
        std::atomic<bool>  stop_flag{false};
        std::atomic<unsigned> in_flight{num_clients};

        auto start_time = std::chrono::steady_clock::now();

        // Launch all simulated clients.
        for (unsigned i = 0; i < num_clients; ++i) {
            auto session = std::make_shared<hs::loadgen::ClientSession>(
                ioc, host, port, metrics, stop_flag, scenario,
                [&in_flight]() {
                    in_flight.fetch_sub(1, std::memory_order_relaxed);
                },
                i);
            session->start();
        }

        // Timer to stop the test after `duration_secs`.
        asio::steady_timer timer(ioc, std::chrono::seconds(duration_secs));
        timer.async_wait([&](boost::system::error_code) {
            stop_flag.store(true, std::memory_order_relaxed);
        });

        // Run io_context on N threads.
        std::vector<std::thread> threads;
        threads.reserve(num_threads - 1);
        for (unsigned i = 1; i < num_threads; ++i) {
            threads.emplace_back([&ioc]() { ioc.run(); });
        }
        ioc.run(); // Main thread also runs.

        for (auto& t : threads) t.join();

        double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start_time).count();

        metrics.print(std::cout, elapsed);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[Fatal] " << ex.what() << '\n';
        return 1;
    }
}
