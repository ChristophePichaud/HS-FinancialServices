# HS-FinancialServices

**High-Performance, Bank-Grade TCP Service built with Boost.Asio and Modern C++17**

---

## What is this?

HS-FinancialServices is a production-style, scalable banking service platform
designed for multi-core Linux servers.  It uses **Boost.Asio** for all
asynchronous I/O, giving you a thread-safe, lock-minimal server that can handle
**hundreds of thousands of requests per second** while keeping P99 latencies
well below 1 ms on loopback.

The repository contains three applications:

| Application | Platform | Description |
|-------------|----------|-------------|
| `hs_server` | Linux / Windows | The banking service (TCP, async, multi-threaded) |
| `hs_loadgen` | Linux / Windows | A Boost.Asio load generator that simulates thousands of concurrent banking clients |
| `hs_ui_client` | **Windows only** | An MFC GUI test client – exercise every server service interactively with a single click |

---

## Quick start (server + load generator)

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

## MFC UI Client (Windows)

`hs_ui_client` is a Windows MFC dialog application that lets you exercise every
banking service through a point-and-click GUI — ideal for demos and integration
testing.

### Features

- **Connection panel** – enter the server host and port, then click **Connect**
  or **Disconnect**.  A colour-coded status indicator shows session state.
- **Command drop-down** – 16 pre-built test cases covering all services:
  Login, Logout, Ping, SQL Queries, Buy/Sell Orders, Get Position, Price
  History, and a **Batch Test** that runs all services in sequence.
- **Output panel** – scrollable, timestamped log of every request and response.
  Click **Clear Output** to reset.
- All network I/O runs on a background thread so the UI stays responsive.

### Build (Visual Studio)

```bat
:: Prerequisites: Visual Studio 2019/2022 with "MFC and ATL support" workload

cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target hs_ui_client
:: Executable: build\ui_client\Release\hs_ui_client.exe
```

### Usage

1. Start `hs_server` on the target machine (default port 9000).
2. Launch `hs_ui_client.exe`.
3. Set **Host** and **Port**, then press **Connect**.
4. Choose a command from the drop-down, press **Run**, and observe the output.

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
- **MFC (Microsoft Foundation Classes)** – Windows GUI for the test client
- **Winsock 2** – TCP networking in the MFC client
- **CMake 3.16+** – build system
