#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace ftxui::ext
{
    enum class PerformanceMetric : std::size_t
    {
        Frame,
        Transcript,
        BlackHoleAdvance,
        BlackHoleRender,
        Count,
    };

    struct PerformanceSummary
    {
        std::uint64_t samples = 0;
        double mean_ms = 0.0;
        double p95_ms = 0.0;
        double max_ms = 0.0;
    };

    struct PerformanceSnapshot
    {
        std::array<PerformanceSummary, static_cast<std::size_t>(PerformanceMetric::Count)> metrics;
    };

    // Intentionally small and allocation-free on the hot path. This is a
    // first-pass UI profiler, not a tracing system: it retains only the last
    // 120 timings for a useful current p95 and lifetime mean/max.
    class PerformanceStats
    {
    public:
        void Record(PerformanceMetric metric, std::chrono::microseconds elapsed)
        {
            std::lock_guard<std::mutex> lock(mu_);
            Bucket &bucket = buckets_[static_cast<std::size_t>(metric)];
            const double ms = static_cast<double>(elapsed.count()) / 1000.0;
            bucket.total_ms += ms;
            bucket.max_ms = std::max(bucket.max_ms, ms);
            bucket.recent[bucket.next] = ms;
            bucket.next = (bucket.next + 1) % bucket.recent.size();
            bucket.count = std::min(bucket.count + 1, bucket.recent.size());
            ++bucket.samples;
        }

        [[nodiscard]] PerformanceSnapshot Snapshot() const
        {
            std::lock_guard<std::mutex> lock(mu_);
            PerformanceSnapshot snapshot;
            for (std::size_t i = 0; i < buckets_.size(); ++i)
            {
                const Bucket &bucket = buckets_[i];
                PerformanceSummary &out = snapshot.metrics[i];
                out.samples = bucket.samples;
                if (bucket.samples == 0)
                    continue;
                out.mean_ms = bucket.total_ms / static_cast<double>(bucket.samples);
                out.max_ms = bucket.max_ms;
                std::array<double, kWindow> ordered = bucket.recent;
                std::sort(ordered.begin(), ordered.begin() + static_cast<std::ptrdiff_t>(bucket.count));
                const std::size_t p95 = std::min(bucket.count - 1, (bucket.count * 95) / 100);
                out.p95_ms = ordered[p95];
            }
            return snapshot;
        }

        void Reset()
        {
            std::lock_guard<std::mutex> lock(mu_);
            buckets_ = {};
        }

    private:
        static constexpr std::size_t kWindow = 120;
        struct Bucket
        {
            std::array<double, kWindow> recent{};
            std::size_t next = 0;
            std::size_t count = 0;
            std::uint64_t samples = 0;
            double total_ms = 0.0;
            double max_ms = 0.0;
        };

        mutable std::mutex mu_;
        std::array<Bucket, static_cast<std::size_t>(PerformanceMetric::Count)> buckets_{};
    };
} // namespace ftxui::ext
