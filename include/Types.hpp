#pragma once
//
// Types.hpp
// ---------
// Plain data structures shared by every part of the engine. Nothing in here
// depends on Qt, so the whole processing engine can be built and tested
// without a GUI.

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace spectra {

// ---------------------------------------------------------------------------
// Video information
// ---------------------------------------------------------------------------
struct VideoMetadata {
    std::string path;
    std::string fileName;
    double fps = 0.0;
    int width = 0;
    int height = 0;
    long long frameCount = 0;   // as reported by the container (may be 0 if unknown)
    double durationSec = 0.0;
};

// ---------------------------------------------------------------------------
// One unit of work for a worker thread.
//
// Each task carries BOTH the current frame and the previous frame
// (cv::Mat is reference counted, so "previous" is not a pixel copy). That makes
// every task completely independent: a worker never has to wait for another
// worker, and the result does not depend on how many threads are used.
// ---------------------------------------------------------------------------
struct FrameTask {
    std::uint64_t frameId = 0;
    double timestampSec = 0.0;
    cv::Mat frame;
    cv::Mat previous;   // empty for the very first frame
};

// ---------------------------------------------------------------------------
// What the FrameAnalyzer measures for one frame
// ---------------------------------------------------------------------------
struct FrameResult {
    std::uint64_t frameId = 0;
    double timestampSec = 0.0;
    double changedRatio = 0.0;        // 0..1  fraction of pixels that changed a lot
    double largestRegionRatio = 0.0;  // 0..1  biggest connected changed area / frame area
    double histogramDistance = 0.0;   // 0..1  0 = same brightness histogram, 1 = completely different
    double brightnessDelta = 0.0;     // signed change of the average brightness (-255..255)
};

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
enum class EventType {
    SignificantMotion,
    HighActivity,
    SceneChange,
    SuddenVisualChange
};

inline const char* eventTypeName(EventType t)
{
    switch (t) {
    case EventType::SignificantMotion:  return "Significant Motion";
    case EventType::HighActivity:       return "High Activity";
    case EventType::SceneChange:        return "Scene Change";
    case EventType::SuddenVisualChange: return "Sudden Visual Change";
    }
    return "Unknown";
}

struct EventInfo {
    EventType type = EventType::SignificantMotion;
    double startSec = 0.0;
    double endSec = 0.0;
    double score = 0.0;   // 0..1
    double duration() const { return endSec - startSec; }
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct AnalyzerConfig {
    int analysisWidth = 1280;       // frames wider than this are shrunk before analysis (HD is analysed as-is)
    int blurKernel = 9;             // Gaussian blur size (odd number)
    int pixelDiffThreshold = 25;    // a pixel "changed" if it differs by more than this (0..255)
    int histogramBins = 64;
};

struct DetectorConfig {
    bool detectMotion = true;
    bool detectActivity = true;
    bool detectSceneChange = true;

    double motionThreshold = 0.005;     // >= 0.5% of pixels changed -> Significant Motion
    double activityThreshold = 0.10;    // >= 10% of pixels changed -> High Activity
    double sceneChangeRatio = 0.10;     // >= 10% of pixels changed ...
    double sceneChangeHistogram = 0.40; // ... and the brightness histogram moved at least this much -> Scene Change
    double suddenBrightness = 40.0;     // average brightness jump >= 40 levels -> Sudden Visual Change

    double mergeGapSec = 0.75;          // detections closer than this are merged into one event
    int minMotionFrames = 3;            // ignore motion bursts shorter than this many frames
    double scoreFullScaleRatio = 0.25;  // motion score 1.0 == a quarter of the frame changed
    double frameDurationSec = 1.0 / 30; // filled in by the engine from the video FPS
};

struct AnalysisSettings {
    std::string videoPath;
    int workers = 4;              // number of worker threads (1..kMaxWorkers)
    bool useThreadPool = true;    // false = plain single-threaded loop (no queue, no pool)
    long long maxFrames = 0;      // 0 = whole video; otherwise stop after this many frames
    AnalyzerConfig analyzer;
    DetectorConfig detector;
};

constexpr int kMaxWorkers = 64;

// ---------------------------------------------------------------------------
// Measurements
// ---------------------------------------------------------------------------
struct PerfSnapshot {
    double wallSeconds = 0.0;
    double cpuPercent = 0.0;     // process CPU time / (wall time x logical cores) x 100
    double peakMemoryMB = 0.0;   // highest memory use sampled during the run
};

struct ProgressInfo {
    std::uint64_t framesProcessed = 0;
    long long totalFrames = 0;   // 0 = unknown
    double fps = 0.0;            // frames processed per second so far
    int workers = 1;
    std::size_t eventsFound = 0;
    double elapsedSec = 0.0;
};

struct RunResult {
    bool ok = false;               // false = a real error happened (see error)
    bool cancelled = false;        // true = the user stopped the run (results are partial)
    std::string error;             // friendly message when ok == false
    std::vector<std::string> warnings;
    VideoMetadata meta;
    int workers = 1;
    bool multiThreaded = false;
    std::uint64_t framesProcessed = 0;
    double fps = 0.0;
    PerfSnapshot perf;
    std::vector<EventInfo> events;
    std::vector<FrameResult> frameResults;   // ordered by frame id
};

struct BenchmarkRow {
    int threads = 1;
    double timeSec = 0.0;
    double fps = 0.0;
    double speedup = 1.0;
    double efficiency = 100.0;     // percent
    double cpuPercent = 0.0;
    double memoryMB = 0.0;
};

} // namespace spectra
