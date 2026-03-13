# HS-FinancialServices

**High-Performance, Bank-Grade TCP Service built with Boost.Asio and Modern C++17**

---

## What is this?

HS-FinancialServices is a production-style, scalable banking service platform
designed for multi-core Linux servers.  It uses **Boost.Asio** for all
asynchronous I/O, giving you a thread-safe, lock-minimal server that can handle
**hundreds of thousands of requests per second** while keeping P99 latencies
well below 1 ms on loopback.

The repository contains two applications:

| Application | Description |
|-------------|-------------|
| `hs_server` | The banking service (TCP, async, multi-threaded) |
| `hs_loadgen` | A Boost.Asio load generator that simulates thousands of concurrent banking clients |

---

## Quick start

```bash
# Prerequisites (Ubuntu/Debian)
sudo apt-get install -y cmake build-essential \
    libboost-system-dev libboost-thread-dev libboost-program-options-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run the server (uses all CPU cores by default)
./server/hs_server --port 9000

# In another terminal: run a 10-second load test with 100 concurrent clients
./load_generator/hs_loadgen --clients 100 --duration 10
```

---

## Supported banking services

| Service | Request type | Description |
|---------|-------------|-------------|
| SQL Query | `kSqlQuery` | Execute arbitrary SQL against the banking database |
| Buy Order | `kBuyOrder` | Submit a buy order for a financial instrument |
| Sell Order | `kSellOrder` | Submit a sell order |
| Get Position | `kGetPosition` | Retrieve account holdings and P&L |
| Price History | `kGetPriceHistory` | Fetch OHLCV candle data for a symbol |
| Ping | `kPing` | Connectivity heartbeat |

---

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for:

- Threading and I/O model
- Wire protocol specification
- How to add new banking services (step-by-step)
- Build instructions
- Load generator usage and scenario configuration
- How to interpret performance metrics

---

## Performance (sample run on 2-core VM, loopback)

```
╔══════════════════════════════════════════╗
║         Load Generator Results           ║
╠══════════════════════════════════════════╣
║  Duration      :       3.00 s          ║
║  Requests OK   :     250060            ║
║  Errors        :          0            ║
║  Throughput    :   83341.26 req/s      ║
╠══════════════════════════════════════════╣
║  Latency (µs)                            ║
║    Min         :      49.00            ║
║    Mean        :     117.97            ║
║    P50         :     112.00            ║
║    P90         :     166.00            ║
║    P99         :     224.00            ║
║    Max         :    1383.00            ║
╚══════════════════════════════════════════╝
```

---

## Technology

- **C++17** – RAII, smart pointers, structured bindings, `std::atomic`
- **Boost.Asio 1.74+** – async TCP, timer, signal handling
- **Boost.Program_options** – command-line configuration
- **CMake 3.16+** – build system
