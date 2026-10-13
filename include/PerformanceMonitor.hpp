#pragma once
//
// PerformanceMonitor
// ------------------
// Measures a run with REAL numbers (nothing is hard-coded):
//   * execution time      std::chrono::steady_clock
//   * CPU utilisation     process CPU time / (wall time x logical cores) x 100
//                         (Windows: GetProcessTimes, Linux: getrusage)
//   * memory              process memory sampled while running, highest value kept
//                         (Windows: GetProcessMemoryInfo, Linux: /proc/self/statm)

#include "Types.hpp"

#include <atomic>
#include <chrono>

namespace spectra {

class PerformanceMonitor {
public:
    void start();
    void sampleMemory();        // thread-safe; call now and then while running
    PerfSnapshot stop();

    double elapsedSeconds() const;

    static unsigned hardwareThreads();

private:
    std::chrono::steady_clock::time_point startTime_;
    double cpuSecondsAtStart_ = 0.0;
    std::atomic<long long> peakKB_{0};
};

} // namespace spectra
