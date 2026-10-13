#include "VideoProcessor.hpp"

#include "EventDetector.hpp"
#include "FrameAnalyzer.hpp"
#include "PerformanceMonitor.hpp"
#include "ResultManager.hpp"
#include "TaskQueue.hpp"
#include "ThreadPool.hpp"

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <string>

namespace spectra {

namespace {

const char* const kOpenHint =
    "Please choose a valid MP4, AVI or MOV video. If the file is fine, make sure its path has no "
    "special characters (move it to a simple folder such as C:\\Videos) and that OpenCV was built "
    "with video support.";

std::string fileNameOf(const std::string& path)
{
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path;
    }
}

// Opens the video and fills metadata. Returns an empty string on success, otherwise a friendly error.
std::string openVideo(const std::string& path, cv::VideoCapture& cap, VideoMetadata& meta)
{
    if (path.empty()) {
        return "No video selected.";
    }
    std::error_code ec;
    if (!std::filesystem::exists(std::filesystem::path(path), ec)) {
        return "The video file was not found:\n" + path;
    }
    if (!cap.open(path) || !cap.isOpened()) {
        return std::string("Could not open this video (unsupported format or damaged file).\n") + kOpenHint;
    }

    meta.path = path;
    meta.fileName = fileNameOf(path);
    meta.fps = cap.get(cv::CAP_PROP_FPS);
    meta.width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    meta.height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    const double count = cap.get(cv::CAP_PROP_FRAME_COUNT);
    meta.frameCount = (std::isfinite(count) && count > 0) ? static_cast<long long>(count + 0.5) : 0;
    if (!std::isfinite(meta.fps) || meta.fps < 1.0 || meta.fps > 1000.0) {
        meta.fps = 25.0;   // unknown FPS: assume a common value so timestamps still work
    }
    meta.durationSec = meta.frameCount > 0 ? meta.frameCount / meta.fps : 0.0;
    return std::string();
}

} // namespace

// ---------------------------------------------------------------------------
ProbeResult VideoProcessor::probe(const std::string& path)
{
    ProbeResult result;
    cv::VideoCapture cap;
    const std::string error = openVideo(path, cap, result.meta);
    if (!error.empty()) {
        result.error = error;
        return result;
    }

    cv::Mat first;
    if (!cap.read(first) || first.empty()) {
        result.error = std::string("The video opened but no picture could be decoded. "
                                   "The file is empty or corrupted.\n") + kOpenHint;
        return result;
    }
    if (result.meta.width <= 0 || result.meta.height <= 0) {
        result.meta.width = first.cols;
        result.meta.height = first.rows;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
namespace {

// OpenCV has its OWN hidden thread pool (it splits big blur / resize / diff operations over
// several cores). If we leave it on, a "1 thread" run is secretly using many cores, and with
// N worker threads we get N x (OpenCV threads) fighting for the CPU. So during an analysis
// OpenCV is told to use 1 thread: the ONLY parallelism is then our own ThreadPool, which is
// what we want to measure. The previous setting is restored afterwards (RAII).
struct OpenCvThreadGuard {
    int previous;
    OpenCvThreadGuard() : previous(cv::getNumThreads()) { cv::setNumThreads(1); }
    ~OpenCvThreadGuard() { cv::setNumThreads(previous); }
};

} // namespace

RunResult VideoProcessor::run(const AnalysisSettings& settings, const RunHooks& hooks) const
{
    OpenCvThreadGuard openCvGuard;
    RunResult res;
    res.multiThreaded = settings.useThreadPool;
    res.workers = settings.useThreadPool ? settings.workers : 1;

    // ---- validate the request ---------------------------------------------
    if (settings.workers < 1 || settings.workers > kMaxWorkers) {
        res.error = "Invalid thread count. Please choose between 1 and " + std::to_string(kMaxWorkers) + " threads.";
        return res;
    }
    const DetectorConfig& dc = settings.detector;
    if (!dc.detectMotion && !dc.detectActivity && !dc.detectSceneChange) {
        res.error = "Please select at least one analysis type (Motion, Scene Change or Activity).";
        return res;
    }

    auto cancelled = [&hooks]() {
        return hooks.cancel && hooks.cancel->load(std::memory_order_relaxed);
    };

    // ---- open the video ---------------------------------------------------
    cv::VideoCapture cap;
    const std::string openError = openVideo(settings.videoPath, cap, res.meta);
    if (!openError.empty()) {
        res.error = openError;
        return res;
    }

    const double fps = res.meta.fps;
    long long expectedFrames = res.meta.frameCount;
    if (settings.maxFrames > 0 && (expectedFrames == 0 || settings.maxFrames < expectedFrames)) {
        expectedFrames = settings.maxFrames;
    }

    DetectorConfig detectorConfig = settings.detector;
    detectorConfig.frameDurationSec = 1.0 / fps;

    FrameAnalyzer analyzer(settings.analyzer);
    ResultManager results{EventDetector(detectorConfig)};
    PerformanceMonitor perf;

    // ---- throttled progress reporting ---------------------------------------
    using Clock = std::chrono::steady_clock;
    const auto runStart = Clock::now();
    auto nowMs = [&runStart]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - runStart).count();
    };
    std::atomic<long long> lastEmitMs{-1000000};

    auto emitProgress = [&](bool force) {
        if (!hooks.onProgress) {
            return;
        }
        const long long now = nowMs();
        long long last = lastEmitMs.load(std::memory_order_relaxed);
        if (!force) {
            if (now - last < hooks.progressIntervalMs) {
                return;
            }
            // Only one thread wins the right to report this time slice.
            if (!lastEmitMs.compare_exchange_strong(last, now)) {
                return;
            }
        } else {
            lastEmitMs.store(now);
        }
        perf.sampleMemory();
        ProgressInfo p;
        p.framesProcessed = results.framesProcessed();
        p.totalFrames = expectedFrames;
        p.workers = res.workers;
        p.eventsFound = results.eventsFound();
        p.elapsedSec = perf.elapsedSeconds();
        p.fps = p.elapsedSec > 0.0 ? p.framesProcessed / p.elapsedSec : 0.0;
        hooks.onProgress(p);
    };

    // ---- run -----------------------------------------------------------------
    perf.start();

    std::uint64_t frameId = 0;
    cv::Mat previous;
    bool readFailedEarly = false;

    auto readNext = [&](cv::Mat& frame) -> bool {
        if (settings.maxFrames > 0 && static_cast<long long>(frameId) >= settings.maxFrames) {
            return false;
        }
        cv::Mat f;                          // a fresh Mat every time, so frames never share pixels
        if (!cap.read(f) || f.empty()) {
            return false;
        }
        frame = f;
        return true;
    };

    if (!settings.useThreadPool) {
        // ===== SINGLE-THREADED BASELINE =====================================
        cv::Mat frame;
        while (!cancelled() && readNext(frame)) {
            FrameTask task;
            task.frameId = frameId;
            task.timestampSec = frameId / fps;
            task.frame = frame;
            task.previous = previous;
            results.store(analyzer.analyze(task));
            previous = frame;
            ++frameId;
            emitProgress(false);
        }
    } else {
        // ===== MULTI-THREADED: PRODUCER -> QUEUE -> THREAD POOL =============
        const std::size_t capacity = static_cast<std::size_t>(std::max(6, 3 * settings.workers));
        TaskQueue queue(capacity);

        ThreadPool pool(static_cast<std::size_t>(settings.workers), queue, [&](FrameTask& task) {
            FrameResult r;
            try {
                r = analyzer.analyze(task);
            } catch (...) {
                r = FrameResult();               // keep the frame order intact even if one frame fails
                r.frameId = task.frameId;
                r.timestampSec = task.timestampSec;
            }
            results.store(r);
            emitProgress(false);
        });

        cv::Mat frame;
        while (!cancelled() && readNext(frame)) {
            FrameTask task;
            task.frameId = frameId;
            task.timestampSec = frameId / fps;
            task.frame = frame;
            task.previous = previous;
            previous = frame;
            ++frameId;
            if (!queue.push(std::move(task))) {
                break;                            // queue was cancelled
            }
        }

        if (cancelled()) {
            queue.cancel();                       // throw away waiting frames, wake everybody
        } else {
            queue.close();                        // let workers finish what is queued
        }
        pool.join();                              // every worker thread has really stopped here
    }

    readFailedEarly = !cancelled() && expectedFrames > 0 && settings.maxFrames == 0
                      && static_cast<long long>(frameId) < static_cast<long long>(expectedFrames * 0.9);

    res.events = results.finish();
    res.perf = perf.stop();
    res.framesProcessed = results.framesProcessed();
    res.fps = res.perf.wallSeconds > 0.0 ? res.framesProcessed / res.perf.wallSeconds : 0.0;
    res.frameResults = results.orderedResults();
    res.cancelled = cancelled();

    if (res.framesProcessed == 0 && !res.cancelled) {
        res.error = std::string("The video contains no readable frames.\n") + kOpenHint;
        return res;
    }
    if (readFailedEarly) {
        res.warnings.push_back("The video stopped decoding early (" + std::to_string(frameId) + " of about "
                               + std::to_string(expectedFrames) + " frames). The file may be damaged; "
                               "results cover the readable part only.");
    }

    res.ok = true;
    emitProgress(true);
    return res;
}

} // namespace spectra
