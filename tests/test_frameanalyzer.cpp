// FrameAnalyzer: the image measurements behave as described.
#include "FrameAnalyzer.hpp"
#include "test_common.hpp"

#include <opencv2/imgproc.hpp>

#include <thread>
#include <vector>

using namespace spectra;

static cv::Mat background(int w = 320, int h = 180, int level = 50)
{
    return cv::Mat(h, w, CV_8UC3, cv::Scalar(level, level, level));
}

static FrameTask task(const cv::Mat& cur, const cv::Mat& prev)
{
    FrameTask t;
    t.frameId = 1;
    t.timestampSec = 0.033;
    t.frame = cur;
    t.previous = prev;
    return t;
}

int main()
{
    FrameAnalyzer analyzer;

    // identical frames -> nothing changed
    {
        cv::Mat a = background();
        FrameResult r = analyzer.analyze(task(a, a.clone()));
        CHECK_NEAR(r.changedRatio, 0.0, 1e-9);
        CHECK_NEAR(r.histogramDistance, 0.0, 1e-6);
        CHECK_NEAR(r.brightnessDelta, 0.0, 1e-6);
        CHECK(r.frameId == 1);
    }

    // first frame (no previous) -> all zero
    {
        FrameResult r = analyzer.analyze(task(background(), cv::Mat()));
        CHECK_NEAR(r.changedRatio, 0.0, 1e-9);
    }

    // a small box jumps to a new place: about 2 x box area changes
    {
        cv::Mat prev = background();
        cv::Mat cur = background();
        cv::rectangle(prev, cv::Rect(20, 50, 40, 40), cv::Scalar(255, 255, 255), cv::FILLED);
        cv::rectangle(cur, cv::Rect(200, 50, 40, 40), cv::Scalar(255, 255, 255), cv::FILLED);
        FrameResult r = analyzer.analyze(task(cur, prev));
        const double expected = 2.0 * 40 * 40 / (320.0 * 180.0);   // ~5.6 %
        CHECK(r.changedRatio > expected * 0.6);
        CHECK(r.changedRatio < expected * 1.2);
        CHECK(r.largestRegionRatio > 0.0);
        CHECK(r.largestRegionRatio < 0.2);
    }

    // everything gets brighter (a flash)
    {
        cv::Mat prev = background(320, 180, 50);
        cv::Mat cur = background(320, 180, 130);
        FrameResult r = analyzer.analyze(task(cur, prev));
        CHECK_NEAR(r.brightnessDelta, 80.0, 1.5);
        CHECK(r.changedRatio > 0.95);
        CHECK(r.histogramDistance > 0.5);
    }

    // large frames are shrunk first but the measurement still makes sense
    {
        cv::Mat prev = background(1920, 1080, 50);
        cv::Mat cur = background(1920, 1080, 50);
        cv::rectangle(cur, cv::Rect(100, 100, 600, 400), cv::Scalar(255, 255, 255), cv::FILLED);
        FrameResult r = analyzer.analyze(task(cur, prev));
        const double expected = 600.0 * 400.0 / (1920.0 * 1080.0);   // ~11.6 %
        CHECK_NEAR(r.changedRatio, expected, 0.02);
    }

    // thread safety: many threads analysing the same input give identical answers
    {
        cv::Mat prev = background();
        cv::Mat cur = background();
        cv::circle(cur, cv::Point(160, 90), 40, cv::Scalar(0, 0, 255), cv::FILLED);
        const FrameResult ref = analyzer.analyze(task(cur, prev));
        std::vector<FrameResult> out(8);
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&, i] {
                for (int k = 0; k < 50; ++k) out[i] = analyzer.analyze(task(cur, prev));
            });
        }
        for (auto& t : threads) t.join();
        bool same = true;
        for (auto& r : out) same = same && r.changedRatio == ref.changedRatio && r.histogramDistance == ref.histogramDistance;
        CHECK(same);
    }

    return finishTest("test_frameanalyzer");
}
