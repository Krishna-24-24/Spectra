// EventDetector: thresholds, grouping of nearby detections, noise filtering.
#include "EventDetector.hpp"
#include "test_common.hpp"

#include <vector>

using namespace spectra;

static const double FPS = 30.0;

static DetectorConfig config()
{
    DetectorConfig c;
    c.frameDurationSec = 1.0 / FPS;
    return c;
}

// Feeds `frames` quiet frames then returns the next frame id.
struct Feeder {
    EventDetector det;
    std::uint64_t id = 0;
    explicit Feeder(DetectorConfig c) : det(c) {}

    void frame(double ratio, double hist = 0.0, double brightness = 0.0)
    {
        FrameResult r;
        r.frameId = id;
        r.timestampSec = id / FPS;
        r.changedRatio = ratio;
        r.histogramDistance = hist;
        r.brightnessDelta = brightness;
        det.onFrame(r);
        ++id;
    }
    void quiet(int n) { for (int i = 0; i < n; ++i) frame(0.0); }
    void burst(int n, double ratio) { for (int i = 0; i < n; ++i) frame(ratio); }
};

int main()
{
    // --- continuous movement becomes ONE event, not one per frame ------------------
    {
        Feeder f(config());
        f.quiet(60);
        f.burst(60, 0.05);            // 2 seconds of motion
        f.quiet(60);
        auto events = f.det.finish();
        CHECK(events.size() == 1);
        if (!events.empty()) {
            CHECK(events[0].type == EventType::SignificantMotion);
            CHECK_NEAR(events[0].startSec, 2.0, 0.05);
            CHECK_NEAR(events[0].endSec, 4.0, 0.1);
            CHECK_NEAR(events[0].score, 0.05 / 0.25, 1e-9);
        }
    }

    // --- two bursts closer than the merge gap merge; further apart stay separate -----
    {
        Feeder f(config());
        f.burst(10, 0.05);
        f.quiet(10);                  // 0.33 s gap -> merged
        f.burst(10, 0.05);
        f.quiet(60);                  // 2 s gap -> separate
        f.burst(10, 0.05);
        auto events = f.det.finish();
        CHECK(events.size() == 2);
    }

    // --- tiny bursts are noise ------------------------------------------------------------
    {
        Feeder f(config());
        f.quiet(30);
        f.burst(2, 0.05);             // only 2 frames
        f.quiet(60);
        CHECK(f.det.finish().empty());
    }

    // --- strong movement is High Activity -----------------------------------------------------
    {
        Feeder f(config());
        f.quiet(30);
        f.burst(30, 0.20);
        f.quiet(30);
        auto events = f.det.finish();
        CHECK(events.size() == 1);
        if (!events.empty()) {
            CHECK(events[0].type == EventType::HighActivity);
            CHECK_NEAR(events[0].score, 0.8, 1e-9);
        }
    }

    // --- scene change vs sudden visual change -----------------------------------------------------
    {
        Feeder f(config());
        f.quiet(30);
        f.frame(0.9, 0.6, 5.0);       // everything changed, brightness similar -> Scene Change
        f.quiet(90);
        f.frame(1.0, 0.9, 90.0);      // brightness jumped by 90 -> Sudden Visual Change
        f.frame(1.0, 0.9, -90.0);     // and back (merged into the same event)
        f.quiet(30);
        auto events = f.det.finish();
        CHECK(events.size() == 2);
        if (events.size() == 2) {
            CHECK(events[0].type == EventType::SceneChange);
            CHECK_NEAR(events[0].startSec, 1.0, 0.05);
            CHECK(events[1].type == EventType::SuddenVisualChange);
            CHECK_NEAR(events[1].score, 0.9, 1e-9);
        }
    }

    // --- analysis options can be switched off --------------------------------------------------------
    {
        DetectorConfig c = config();
        c.detectSceneChange = false;
        Feeder f(c);
        f.quiet(30);
        f.frame(0.9, 0.6, 5.0);
        f.frame(1.0, 0.9, 90.0);
        f.quiet(30);
        CHECK(f.det.finish().empty());

        DetectorConfig c2 = config();
        c2.detectMotion = false;
        c2.detectActivity = false;
        Feeder g(c2);
        g.burst(30, 0.05);
        g.burst(30, 0.20);
        CHECK(g.det.finish().empty());

        DetectorConfig c3 = config();
        c3.detectActivity = false;    // strong movement is then reported as normal motion
        Feeder h(c3);
        h.burst(30, 0.20);
        auto events = h.det.finish();
        CHECK(events.size() == 1);
        if (!events.empty()) CHECK(events[0].type == EventType::SignificantMotion);
    }

    // --- live counter counts open events too --------------------------------------------------------------
    {
        Feeder f(config());
        f.quiet(10);
        CHECK(f.det.eventCount() == 0);
        f.burst(5, 0.05);
        CHECK(f.det.eventCount() == 1);
    }

    return finishTest("test_eventdetector");
}
