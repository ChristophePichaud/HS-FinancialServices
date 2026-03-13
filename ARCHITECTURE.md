# HS-FinancialServices Architecture

## Overview

HS-FinancialServices is a **bank-grade, high-performance TCP server** built on
**Boost.Asio** and **Modern C++17**.  It is designed to:

- Run on multi-core Linux servers and saturate all available CPU cores.
- Handle **thousands of concurrent connections** and **hundreds of thousands of
  requests per second**.
- Allow the bank to add new financial services (new message types and handlers)
  without touching the core I/O or protocol code.

---

## Threading model

```
┌──────────────────────────────────────────────────────────────┐
│                        io_context                            │
│                                                              │
│  Thread-0  Thread-1  Thread-2  …  Thread-N                   │
│  (run)     (run)     (run)         (run)                     │
│     │          │         │              │                    │
│     └──────────┴─────────┴──────────────┘                    │
│                         │                                    │
│             async I/O completions                            │
│                         │                                    │
│           ┌─────────────▼──────────────┐                     │
│           │  Acceptor  (do_accept)      │                     │
│           └─────────────┬──────────────┘                     │
│                         │  new socket                        │
│              ┌──────────▼──────────────┐                     │
│              │  Session (shared_ptr)    │ ×N                  │
│              │  async_read → dispatch   │                     │
│              │  → async_write → loop   │                     │
│              └─────────────────────────┘                     │
└──────────────────────────────────────────────────────────────┘
```

### Key design choices

| Choice | Rationale |
|--------|-----------|
| Single `io_context`, N threads | Simpler than one-io_context-per-thread; Boost.Asio's internal work stealing ensures all threads stay busy. |
| No explicit `strand` per session | Each session only ever has one outstanding `async_read` **or** one outstanding `async_write` at a time (never both concurrently), so the implicit serialisation of composed operations is sufficient. |
| `TCP_NODELAY` enabled | Banking traffic requires the lowest possible latency; Nagle's algorithm would add unnecessary buffering. |
| `SO_REUSEADDR` | Allows rapid server restart without `TIME_WAIT` blocking the port. |
| Atomic counters for metrics | Lock-free: hot-path counter increments never contend. |

---

## Wire protocol

```
 0               1               2               3
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
├───────────────────────────────────────────────────────────────┤
│                     payload_length (32-bit, big-endian)       │
├───────────────────────────────┬───────────────┬───────────────┤
│   message_type (16-bit, BE)   │     flags     │   reserved    │
├───────────────────────────────┴───────────────┴───────────────┤
│                     payload (payload_length bytes)             │
│                     UTF-8 JSON string                          │
└───────────────────────────────────────────────────────────────┘
```

Total fixed overhead: **8 bytes** per message.

### Message type encoding

- **Client → Server**: `message_type < 0x8000`
- **Server → Client**: `message_type ≥ 0x8000`
- Response type = request type with bit-15 set (`type | 0x8000`).

### Defined message types

| Type       | Value  | Direction  | Description |
|------------|--------|------------|-------------|
| `kSqlQuery`        | 0x0001 | C→S | Execute a SQL query |
| `kBuyOrder`        | 0x0002 | C→S | Submit a buy order |
| `kSellOrder`       | 0x0003 | C→S | Submit a sell order |
| `kGetPosition`     | 0x0004 | C→S | Query account positions |
| `kGetPriceHistory` | 0x0005 | C→S | Fetch OHLCV price history |
| `kPing`            | 0x0006 | C→S | Heartbeat / connectivity test |
| `kSqlResult`       | 0x8001 | S→C | SQL query result |
| `kOrderAck`        | 0x8002 | S→C | Order acknowledgement |
| `kPositionResult`  | 0x8004 | S→C | Account positions |
| `kPriceHistResult` | 0x8005 | S→C | Price history data |
| `kPong`            | 0x8006 | S→C | Ping response |
| `kError`           | 0xFFFF | S→C | Error response |

---

## Request routing

```
Session::do_read_body()
        │
        ▼
ServiceRouter::dispatch(Message& request)
        │
        ├── handlers_[kSqlQuery]       → handle_sql_query()
        ├── handlers_[kBuyOrder]       → handle_buy_order()
        ├── handlers_[kSellOrder]      → handle_sell_order()
        ├── handlers_[kGetPosition]    → handle_get_position()
        ├── handlers_[kGetPriceHistory]→ handle_get_price_history()
        ├── handlers_[kPing]           → echo payload
        └── (unknown type)             → kError response
```

The `ServiceRouter` is a simple `unordered_map<uint16_t, ServiceHandler>`.
It is written once at startup and only read thereafter, so it requires no
locking at runtime.

---

## How to add a new banking service

**Example: adding a `GetAccountStatement` service.**

### Step 1 – Define the message type (`server/include/protocol.hpp`)

```cpp
enum class MessageType : uint16_t {
    // … existing types …
    kGetAccountStatement = 0x0007,   // C→S
    kAccountStatement    = 0x8007,   // S→C (auto-derived)
};
```

### Step 2 – Write the handler header (`server/include/services/statement_service.hpp`)

```cpp
#pragma once
#include <string>
namespace hs::services {
    std::string handle_get_account_statement(const std::string& request_json);
}
```

### Step 3 – Implement the handler (`server/src/services/statement_service.cpp`)

```cpp
#include "services/statement_service.hpp"
namespace hs::services {
    std::string handle_get_account_statement(const std::string& payload) {
        // Parse request, call DB, build response JSON.
        return R"({"account":"ACC-001","transactions":[…]})";
    }
}
```

### Step 4 – Register the handler (`server/src/main.cpp`)

```cpp
router.register_handler(hs::MessageType::kGetAccountStatement,
    hs::services::handle_get_account_statement);
```

### Step 5 – Add the `.cpp` to the build (`server/CMakeLists.txt`)

```cmake
add_executable(hs_server
    …
    src/services/statement_service.cpp   # ← add this line
)
```

**The core I/O, session management, and protocol code are untouched.**

---

## Project structure

```
HS-FinancialServices/
├── CMakeLists.txt                     # Top-level build
├── README.md
├── ARCHITECTURE.md                    # This file
│
├── server/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── protocol.hpp               # Wire format, MessageType, encode/decode
│   │   ├── service_router.hpp         # Handler registry + dispatch
│   │   ├── server.hpp                 # io_context, acceptor, thread pool
│   │   ├── session.hpp                # Per-connection async read/write loop
│   │   └── services/
│   │       ├── sql_service.hpp
│   │       ├── trade_service.hpp
│   │       ├── position_service.hpp
│   │       └── price_history_service.hpp
│   └── src/
│       ├── main.cpp                   # Server entry point + service registration
│       ├── server.cpp
│       ├── session.cpp
│       └── services/
│           ├── sql_service.cpp
│           ├── trade_service.cpp
│           ├── position_service.cpp
│           └── price_history_service.cpp
│
└── load_generator/
    ├── CMakeLists.txt
    ├── include/
    │   ├── client_session.hpp         # One simulated client (Boost.Asio)
    │   └── metrics_collector.hpp      # Thread-safe latency/RPS stats
    └── src/
        ├── main.cpp                   # Load generator entry point
        ├── client_session.cpp
        └── metrics_collector.cpp
```

---

## Building

### Prerequisites

- CMake ≥ 3.16
- GCC ≥ 9 or Clang ≥ 10 with C++17 support
- Boost ≥ 1.74 (`system`, `thread`, `program_options`)

```bash
# Ubuntu / Debian
sudo apt-get install -y cmake build-essential \
    libboost-system-dev libboost-thread-dev libboost-program-options-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

The two executables are placed at:
- `build/server/hs_server`
- `build/load_generator/hs_loadgen`

---

## Running the server

```bash
./build/server/hs_server [options]

Options:
  -a, --address  <addr>   Bind address (default: 0.0.0.0)
  -p, --port     <port>   TCP port      (default: 9000)
  -t, --threads  <n>      Worker threads (default: hardware_concurrency)
  -m, --max-connections   Max concurrent connections (default: 50000)
```

Example:
```bash
./build/server/hs_server --port 9000 --threads 8
```

Graceful shutdown: press `Ctrl-C` or send `SIGTERM`.

---

## Running the load generator

```bash
./build/load_generator/hs_loadgen [options]

Options:
  --host              <host>   Server hostname (default: 127.0.0.1)
  -p, --port          <port>   Server port     (default: 9000)
  -c, --clients       <n>      Concurrent clients (default: 100)
  -d, --duration      <secs>   Test duration in seconds (default: 10)
  -t, --threads       <n>      I/O threads (default: hardware_concurrency)
  --sql-weight        <n>      Relative weight for SQL requests   (default: 20)
  --buy-weight        <n>      Relative weight for Buy orders     (default: 25)
  --sell-weight       <n>      Relative weight for Sell orders    (default: 25)
  --position-weight   <n>      Relative weight for GetPosition    (default: 20)
  --history-weight    <n>      Relative weight for PriceHistory   (default: 10)
```

### Scenarios

**Latency test** (few clients, measure raw latency):
```bash
./hs_loadgen --clients 10 --duration 10
```

**Throughput test** (many clients, saturate server):
```bash
./hs_loadgen --clients 1000 --duration 30
```

**Mixed banking workload**:
```bash
./hs_loadgen --clients 500 --duration 60 \
    --buy-weight 30 --sell-weight 30 \
    --position-weight 20 --sql-weight 15 --history-weight 5
```

---

## Interpreting the performance report

```
╔══════════════════════════════════════════╗
║         Load Generator Results           ║
╠══════════════════════════════════════════╣
║  Duration      :      10.00 s          ║
║  Requests OK   :    850000             ║  ← completed without error
║  Errors        :          0            ║  ← connection/protocol errors
║  Throughput    :  85000.00 req/s       ║  ← requests per second
╠══════════════════════════════════════════╣
║  Latency (µs)                            ║
║    Min         :      45.00            ║  ← fastest round-trip
║    Mean        :     120.00            ║  ← average latency
║    P50         :     115.00            ║  ← 50% of requests under this
║    P90         :     160.00            ║  ← 90% of requests under this
║    P99         :     220.00            ║  ← SLA target (e.g. ≤ 1 ms)
║    Max         :    1200.00            ║  ← worst case (outlier)
╚══════════════════════════════════════════╝
```

### What to watch

| Metric | Target | Action if exceeded |
|--------|--------|--------------------|
| P99 latency | < 1 ms | Increase `--threads`, check CPU pinning, profile hot path |
| Error count | 0 | Check server logs; may indicate resource exhaustion |
| Throughput | > 100k req/s | Normal for loopback; WAN will be lower due to RTT |
| Max latency | < 10× P99 | GC pauses (not an issue in C++), OS scheduling jitter |

---

## Scalability notes

- **CPU cores**: set `--threads` equal to the number of physical cores.  
  Hyper-threading provides diminishing returns for I/O-bound workloads.
- **Heavy business logic**: if handlers perform blocking work (real DB calls,
  heavy computation), offload them to a separate `boost::asio::thread_pool`
  and post the response back to the session's executor.
- **Connection limits**: Linux default `ulimit -n` is 1024.  
  Increase with `ulimit -n 100000` or via `/etc/security/limits.conf`.
- **Buffer tuning**: increase `net.core.somaxconn` and
  `net.ipv4.tcp_max_syn_backlog` for very high connection rates.
