// End-to-end engine test: real video files, 1/2/4/8 threads, cancel, errors, math.
#include "BenchmarkManager.hpp"
#include "ReportWriter.hpp"
#include "VideoProcessor.hpp"
#include "test_common.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <thread>

using namespace spectra;

// Makes a small MJPG avi: still background, a box moving 2s..4s, a hard cut at 6s.
static std::string makeVideo(const std::string& path)
{
    const int W = 320, H = 180, FPS = 30, N = 240;
    cv::VideoWriter w(path, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), FPS, cv::Size(W, H));
    if (!w.isOpened()) return "";
    for (int i = 0; i < N; ++i) {
        double t = i / double(FPS);
        cv::Mat f(H, W, CV_8UC3, cv::Scalar(60, 60, 60));
        if (t >= 6.0) {   // hard cut to a checkerboard with the SAME average brightness
            for (int y = 0; y < H; y += 40)
                for (int x = 0; x < W; x += 40)
                    cv::rectangle(f, cv::Rect(x, y, 40, 40),
                                  ((x / 40 + y / 40) % 2) ? cv::Scalar(100, 100, 100) : cv::Scalar(20, 20, 20), cv::FILLED);
        }
        if (t >= 2.0 && t < 4.0) {
            int x = int(10 + (t - 2.0) * 120);
            cv::rectangle(f, cv::Rect(x, 60, 60, 60), cv::Scalar(255, 255, 255), cv::FILLED);
        }
        w.write(f);
    }
    return path;
}

static RunResult runWith(const std::string& video, int threads, bool pool, long long maxFrames = 0,
                         const RunHooks& hooks = RunHooks())
{
    AnalysisSettings s;
    s.videoPath = video;
    s.workers = threads;
    s.useThreadPool = pool;
    s.maxFrames = maxFrames;
    return VideoProcessor().run(s, hooks);
}

static bool sameEvents(const RunResult& a, const RunResult& b)
{
    if (a.events.size() != b.events.size()) return false;
    for (size_t i = 0; i < a.events.size(); ++i) {
        if (a.events[i].type != b.events[i].type) return false;
        if (std::fabs(a.events[i].startSec - b.events[i].startSec) > 1e-9) return false;
        if (std::fabs(a.events[i].endSec - b.events[i].endSec) > 1e-9) return false;
        if (std::fabs(a.events[i].score - b.events[i].score) > 1e-9) return false;
    }
    return true;
}

int main()
{
    const std::string video = makeVideo("spectra_test_video.avi");
    CHECK(!video.empty());
    if (video.empty()) return finishTest("test_engine");

    // --- probe ----------------------------------------------------------------
    {
        ProbeResult p = VideoProcessor::probe(video);
        CHECK(p.ok);
        CHECK(p.meta.width == 320 && p.meta.height == 180);
        CHECK_NEAR(p.meta.fps, 30.0, 0.5);
        CHECK(p.meta.frameCount == 240);
    }

    // --- same events for every thread count ---------------------------------------
    RunResult base = runWith(video, 1, false);
    CHECK(base.ok);
    CHECK(base.framesProcessed == 240);
    for (const auto& e : base.events) std::cerr << "   event: " << eventTypeName(e.type) << " @" << e.startSec << " dur " << e.duration() << " score " << e.score << "\n";
    CHECK(base.events.size() == 2);       // the moving box + the hard cut
    if (base.events.size() == 2) {
        CHECK(base.events[0].type == EventType::SignificantMotion || base.events[0].type == EventType::HighActivity);
        CHECK_NEAR(base.events[0].startSec, 2.0, 0.2);
        CHECK(base.events[1].type == EventType::SceneChange);
        CHECK_NEAR(base.events[1].startSec, 6.0, 0.1);
    }
    for (int n : {1, 2, 4, 8}) {
        RunResult r = runWith(video, n, true);
        CHECK(r.ok);
        CHECK(r.framesProcessed == 240);
        CHECK(sameEvents(base, r));
        CHECK(r.frameResults.size() == 240);
        bool ordered = true;
        for (size_t i = 0; i < r.frameResults.size(); ++i) ordered = ordered && r.frameResults[i].frameId == i;
        CHECK(ordered);
    }

    // --- maxFrames ---------------------------------------------------------------------
    {
        RunResult r = runWith(video, 4, true, 50);
        CHECK(r.ok);
        CHECK(r.framesProcessed == 50);
    }

    // --- cancel: returns promptly, no hang, flagged as cancelled ---------------------------
    {
        std::atomic<bool> cancel{false};
        RunHooks hooks;
        hooks.cancel = &cancel;
        hooks.onProgress = [&](const ProgressInfo& p) { if (p.framesProcessed > 20) cancel = true; };
        hooks.progressIntervalMs = 0;
        RunResult r = runWith(video, 4, true, 0, hooks);
        CHECK(r.cancelled);
        CHECK(r.framesProcessed < 240);

        std::atomic<bool> cancel2{true};  // cancelled before it even starts
        RunHooks h2;
        h2.cancel = &cancel2;
        RunResult r2 = runWith(video, 2, true, 0, h2);
        CHECK(r2.cancelled || !r2.ok || r2.framesProcessed == 0);
    }

    // --- friendly errors ------------------------------------------------------------------
    {
        RunResult r = runWith("this_file_does_not_exist.mp4", 4, true);
        CHECK(!r.ok);
        CHECK(!r.error.empty());

        { std::ofstream bad("not_a_video.mp4"); bad << "hello, I am not a video"; }
        RunResult r2 = runWith("not_a_video.mp4", 4, true);
        CHECK(!r2.ok);
        CHECK(!r2.error.empty());
        std::remove("not_a_video.mp4");

        RunResult r3 = runWith(video, 0, true);
        CHECK(!r3.ok);
        RunResult r4 = runWith(video, 1000, true);
        CHECK(!r4.ok);
    }

    // --- real sample video vs ground truth -----------------------------------------------------
    if (const char* sample = std::getenv("SPECTRA_SAMPLE_VIDEO")) {
        std::ifstream probe(sample, std::ios::binary);
        if (probe) {
            RunResult r = runWith(sample, 4, true);
            CHECK(r.ok);
            CHECK(r.events.size() == 5);
            if (r.events.size() == 5) {
                CHECK(r.events[0].type == EventType::SignificantMotion);
                CHECK_NEAR(r.events[0].startSec, 5.0, 0.5);
                CHECK(r.events[1].type == EventType::SceneChange);
                CHECK_NEAR(r.events[1].startSec, 11.0, 0.2);
                CHECK(r.events[2].type == EventType::HighActivity);
                CHECK_NEAR(r.events[2].startSec, 16.0, 0.5);
                CHECK(r.events[3].type == EventType::SuddenVisualChange);
                CHECK_NEAR(r.events[3].startSec, 22.0, 0.2);
                CHECK(r.events[4].type == EventType::SignificantMotion);
                CHECK_NEAR(r.events[4].startSec, 25.0, 0.5);
            }
            RunResult s = runWith(sample, 1, false);
            CHECK(sameEvents(r, s));
        }
    }

    // --- CSV writers -----------------------------------------------------------------------------
    {
        std::string err;
        CHECK(writeEventReport("spectra_test_events.csv", base.events, err));
        std::ifstream f("spectra_test_events.csv");
        std::string header;
        std::getline(f, header);
        CHECK(header == "timestamp,event_type,score,duration");
        std::vector<BenchmarkRow> rows(2);
        rows[0].threads = 1; rows[1].threads = 2;
        CHECK(writeBenchmarkReport("spectra_test_bench.csv", rows, err));
        std::ifstream g("spectra_test_bench.csv");
        std::getline(g, header);
        CHECK(header.rfind("threads,time,fps,speedup,efficiency", 0) == 0);
        CHECK(formatTimestamp(151.0) == "00:02:31");
        CHECK(!writeEventReport("spectra_test_video.avi/x.csv", base.events, err));   // parent is a file -> must fail politely
        CHECK(!err.empty());
        std::remove("spectra_test_events.csv");
        std::remove("spectra_test_bench.csv");
    }

    // --- benchmark maths and interpretation -----------------------------------------------------------
    {
        std::vector<BenchmarkRow> rows(3);
        rows[0].threads = 1; rows[0].timeSec = 10.0;
        rows[1].threads = 2; rows[1].timeSec = 5.5;
        rows[2].threads = 4; rows[2].timeSec = 3.0;
        BenchmarkManager::computeDerived(rows);
        CHECK_NEAR(rows[0].speedup, 1.0, 1e-9);
        CHECK_NEAR(rows[1].speedup, 10.0 / 5.5, 1e-9);
        CHECK_NEAR(rows[1].efficiency, 10.0 / 5.5 / 2 * 100, 1e-9);
        CHECK_NEAR(rows[2].speedup, 10.0 / 3.0, 1e-9);
        CHECK_NEAR(rows[2].efficiency, 10.0 / 3.0 / 4 * 100, 1e-9);
        auto lines = BenchmarkManager::interpret(rows);
        CHECK(lines.size() >= 3);
        CHECK(lines[0].find("4 threads") != std::string::npos);   // best configuration is named

        std::vector<BenchmarkRow> worse(2);
        worse[0].threads = 1; worse[0].timeSec = 2.0;
        worse[1].threads = 2; worse[1].timeSec = 3.0;
        BenchmarkManager::computeDerived(worse);
        auto l2 = BenchmarkManager::interpret(worse);
        CHECK(l2[0].find("did not make") != std::string::npos);
    }

    // --- a real (short) benchmark ------------------------------------------------------------------------
    {
        BenchmarkOptions o;
        o.videoPath = video;
        o.threadCounts = {1, 2, 4};
        o.repeats = 1;
        BenchmarkResult br = BenchmarkManager().run(o);
        CHECK(br.ok);
        CHECK(br.rows.size() == 3);
        if (br.rows.size() == 3) {
            CHECK_NEAR(br.rows[0].speedup, 1.0, 1e-9);
            CHECK(br.rows[2].timeSec > 0.0);
        }
        CHECK(!br.interpretation.empty());

        BenchmarkOptions bad = o;
        bad.threadCounts = {0, 2};
        CHECK(!BenchmarkManager().run(bad).ok);
    }

    std::remove(video.c_str());
    return finishTest("test_engine");
}
