// metrics_collector.cpp

#include "metrics_collector.hpp"

#include <iostream>
#include <iomanip>
#include <numeric>
#include <cmath>

namespace hs::loadgen {

// ---------------------------------------------------------------------------
void MetricsCollector::record_success(DurationUs latency_us) noexcept {
    successes_.fetch_add(1, std::memory_order_relaxed);
    int64_t us = latency_us.count();
    std::lock_guard<std::mutex> lk(samples_mutex_);
    samples_us_.push_back(us);
}

void MetricsCollector::record_error() noexcept {
    errors_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
uint64_t MetricsCollector::successes() const noexcept {
    return successes_.load(std::memory_order_relaxed);
}

uint64_t MetricsCollector::errors() const noexcept {
    return errors_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
MetricsCollector::Stats MetricsCollector::compute(double elapsed_seconds) const {
    Stats s{};
    s.count  = successes_.load(std::memory_order_relaxed);
    s.errors = errors_.load(std::memory_order_relaxed);
    s.rps    = (elapsed_seconds > 0) ? static_cast<double>(s.count) / elapsed_seconds : 0.0;

    std::vector<int64_t> sorted;
    {
        std::lock_guard<std::mutex> lk(samples_mutex_);
        sorted = samples_us_;
    }
    if (sorted.empty()) return s;

    std::sort(sorted.begin(), sorted.end());

    s.min_us  = static_cast<double>(sorted.front());
    s.max_us  = static_cast<double>(sorted.back());

    double sum = 0;
    for (auto v : sorted) sum += static_cast<double>(v);
    s.mean_us = sum / static_cast<double>(sorted.size());

    auto percentile = [&](double p) -> double {
        if (sorted.empty()) return 0;
        std::size_t idx = static_cast<std::size_t>(p * (sorted.size() - 1));
        return static_cast<double>(sorted[idx]);
    };
    s.p50_us = percentile(0.50);
    s.p90_us = percentile(0.90);
    s.p99_us = percentile(0.99);

    return s;
}

// ---------------------------------------------------------------------------
void MetricsCollector::print(std::ostream& out, double elapsed_seconds) const {
    auto s = compute(elapsed_seconds);
    out << "╔══════════════════════════════════════════╗\n"
        << "║         Load Generator Results           ║\n"
        << "╠══════════════════════════════════════════╣\n"
        << std::fixed << std::setprecision(2)
        << "║  Duration      : " << std::setw(10) << elapsed_seconds  << " s          ║\n"
        << "║  Requests OK   : " << std::setw(10) << s.count          << "            ║\n"
        << "║  Errors        : " << std::setw(10) << s.errors         << "            ║\n"
        << "║  Throughput    : " << std::setw(10) << s.rps            << " req/s      ║\n"
        << "╠══════════════════════════════════════════╣\n"
        << "║  Latency (µs)                            ║\n"
        << "║    Min         : " << std::setw(10) << s.min_us         << "            ║\n"
        << "║    Mean        : " << std::setw(10) << s.mean_us        << "            ║\n"
        << "║    P50         : " << std::setw(10) << s.p50_us         << "            ║\n"
        << "║    P90         : " << std::setw(10) << s.p90_us         << "            ║\n"
        << "║    P99         : " << std::setw(10) << s.p99_us         << "            ║\n"
        << "║    Max         : " << std::setw(10) << s.max_us         << "            ║\n"
        << "╚══════════════════════════════════════════╝\n";
}

} // namespace hs::loadgen
