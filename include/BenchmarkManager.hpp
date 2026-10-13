#pragma once
//
// BenchmarkManager
// ----------------
// Runs the SAME analysis again and again with different thread counts and
// records the measured numbers:
//
//     Speedup    = T(1 thread) / T(N threads)
//     Efficiency = Speedup / N x 100 %
//
// "1 thread" is the true single-threaded loop (no queue, no pool), so the
// speedup shows what the producer-consumer + thread pool really gained.
//
// A short warm-up run (first ~300 frames, not recorded) is done first so the
// operating system has the video file in its cache and the first measured run
// is not unfairly slow.

#include "Types.hpp"
#include "VideoProcessor.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace spectra {

struct BenchmarkOptions {
    std::string videoPath;
    std::vector<int> threadCounts{1, 2, 4, 8};
    int repeats = 2;                 // every thread count is measured this many times (mean is used)
    long long maxFrames = 0;         // 0 = whole video
    AnalyzerConfig analyzer;
    DetectorConfig detector;
};

struct BenchmarkHooks {
    // Called after each finished run: (runsDone, runsTotal, text such as "4 threads (run 2/2)")
    std::function<void(int, int, const std::string&)> onStep;
    // Frame-level progress of the run that is currently executing.
    std::function<void(const ProgressInfo&)> onProgress;
    const std::atomic<bool>* cancel = nullptr;
};

struct BenchmarkResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    VideoMetadata meta;
    std::vector<BenchmarkRow> rows;               // sorted by thread count
    std::vector<std::string> interpretation;      // generated from the measured rows
};

class BenchmarkManager {
public:
    BenchmarkResult run(const BenchmarkOptions& options, const BenchmarkHooks& hooks = BenchmarkHooks()) const;

    // Fills speedup + efficiency from the 1-thread row (rows must contain threads == 1).
    static void computeDerived(std::vector<BenchmarkRow>& rows);

    // Plain-English summary generated from the numbers (nothing is hard-coded).
    static std::vector<std::string> interpret(const std::vector<BenchmarkRow>& rows);
};

} // namespace spectra
