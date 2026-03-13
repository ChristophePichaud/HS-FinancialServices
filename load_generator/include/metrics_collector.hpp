// metrics_collector.hpp
// Thread-safe aggregation of latency samples and request counts.
//
// Design: lock-free atomic counters for hot-path increments; a mutex-protected
// vector for latency samples.  At report time, samples are sorted once.

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <string>
#include <ostream>

namespace hs::loadgen {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using DurationUs = std::chrono::microseconds;

class MetricsCollector {
public:
    void record_success(DurationUs latency_us) noexcept;
    void record_error()  noexcept;

    // Snapshot counters (relaxed – for display only).
    uint64_t successes() const noexcept;
    uint64_t errors()    const noexcept;

    struct Stats {
        uint64_t count;
        uint64_t errors;
        double   rps;
        double   mean_us;
        double   p50_us;
        double   p90_us;
        double   p99_us;
        double   min_us;
        double   max_us;
    };

    /// Compute statistics over the collected samples.
    /// safe to call from a single reporter thread.
    Stats compute(double elapsed_seconds) const;

    void print(std::ostream& out, double elapsed_seconds) const;

private:
    std::atomic<uint64_t>     successes_{0};
    std::atomic<uint64_t>     errors_{0};

    mutable std::mutex        samples_mutex_;
    std::vector<int64_t>      samples_us_;  // microseconds
};

} // namespace hs::loadgen
