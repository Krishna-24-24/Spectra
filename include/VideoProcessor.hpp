#pragma once
//
// VideoProcessor  (the "Video Processing Engine")
// -----------------------------------------------
// Runs one complete analysis of one video.
//
//   Multi-thread mode                          Single-thread mode
//   -----------------                          ------------------
//   Producer (this thread) reads frames        One loop: read -> analyze -> store
//        |  push                               (no queue, no pool - the honest
//        v                                      baseline for speedup numbers)
//   TaskQueue (bounded, mutex + condvar)
//        |  pop
//        v
//   ThreadPool: N workers run FrameAnalyzer
//        |  store
//        v
//   ResultManager (re-orders by frame ID) -> EventDetector
//
// The class has no Qt code. Progress is reported through a plain callback, and
// cancellation is a plain std::atomic<bool> that the caller can set from any thread.

#include "Types.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace spectra {

struct ProbeResult {
    bool ok = false;
    std::string error;
    VideoMetadata meta;
};

struct RunHooks {
    // Called from worker threads (throttled). The callback must be thread-safe.
    std::function<void(const ProgressInfo&)> onProgress;
    // Set to true (from any thread) to stop the run safely.
    const std::atomic<bool>* cancel = nullptr;
    int progressIntervalMs = 100;
};

class VideoProcessor {
public:
    // Open the file, read its properties and make sure the first frame can be decoded.
    static ProbeResult probe(const std::string& path);

    // Analyse the whole video (or settings.maxFrames frames).
    RunResult run(const AnalysisSettings& settings, const RunHooks& hooks = RunHooks()) const;
};

} // namespace spectra
