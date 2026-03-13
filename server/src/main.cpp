// main.cpp (server)
// Entry point for the HS-FinancialServices banking server.
//
// Usage:
//   hs_server [--address <addr>] [--port <port>] [--threads <n>]
//             [--max-connections <n>]

#include "server.hpp"
#include "services/sql_service.hpp"
#include "services/trade_service.hpp"
#include "services/position_service.hpp"
#include "services/price_history_service.hpp"

#include <boost/program_options.hpp>

#include <iostream>
#include <stdexcept>

namespace po = boost::program_options;

// ---------------------------------------------------------------------------
// Register all banking service handlers with the router.
// To add a new service: add a new register_handler call here.
// ---------------------------------------------------------------------------
static void register_services(hs::ServiceRouter& router) {
    router.register_handler(hs::MessageType::kSqlQuery,
        hs::services::handle_sql_query);

    router.register_handler(hs::MessageType::kBuyOrder,
        hs::services::handle_buy_order);

    router.register_handler(hs::MessageType::kSellOrder,
        hs::services::handle_sell_order);

    router.register_handler(hs::MessageType::kGetPosition,
        hs::services::handle_get_position);

    router.register_handler(hs::MessageType::kGetPriceHistory,
        hs::services::handle_get_price_history);

    // Ping handler: echo back the sequence number.
    router.register_handler(hs::MessageType::kPing,
        [](const std::string& payload) -> std::string {
            return payload.empty() ? R"({"seq":0})" : payload;
        });
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    try {
        hs::ServerConfig config;

        po::options_description desc("HS-FinancialServices server options");
        desc.add_options()
            ("help,h",    "Show help")
            ("address,a", po::value<std::string>(&config.address)->default_value("0.0.0.0"),
                          "Bind address")
            ("port,p",    po::value<uint16_t>(&config.port)->default_value(9000),
                          "TCP port")
            ("threads,t", po::value<unsigned>(&config.thread_count)->default_value(0),
                          "Worker thread count (0 = hardware_concurrency)")
            ("max-connections,m",
                          po::value<std::size_t>(&config.max_connections)->default_value(50000),
                          "Max concurrent connections");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help")) {
            std::cout << desc << '\n';
            return 0;
        }

        hs::Server server(config);
        register_services(server.router());
        server.run(); // Blocks until SIGINT/SIGTERM.
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "[Fatal] " << ex.what() << '\n';
        return 1;
    }
}
