// ResultManager: results arriving in the WRONG order are still processed in frame order.
#include "ResultManager.hpp"
#include "test_common.hpp"

#include <algorithm>
#include <random>
#include <thread>
#include <vector>

using namespace spectra;

static FrameResult make(std::uint64_t id, double ratio)
{
    FrameResult r;
    r.frameId = id;
    r.timestampSec = id / 30.0;
    r.changedRatio = ratio;
    return r;
}

static std::vector<FrameResult> data()
{
    std::vector<FrameResult> v;
    for (std::uint64_t i = 0; i < 600; ++i) {
        const bool burst = (i >= 100 && i < 160) || (i >= 400 && i < 430);
        v.push_back(make(i, burst ? 0.06 : 0.0));
    }
    return v;
}

int main()
{
    DetectorConfig c;
    c.frameDurationSec = 1.0 / 30.0;

    // reference: results fed in perfect order
    ResultManager ordered{EventDetector(c)};
    for (const auto& r : data()) ordered.store(r);
    auto refEvents = ordered.finish();
    CHECK(refEvents.size() == 2);

    // shuffled and delivered by 4 threads at once
    for (int seed = 1; seed <= 5; ++seed) {
        auto v = data();
        std::mt19937 rng(seed);
        std::shuffle(v.begin(), v.end(), rng);

        ResultManager mgr{EventDetector(c)};
        std::vector<std::thread> threads;
        const size_t chunk = v.size() / 4;
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&, t] {
                const size_t begin = t * chunk;
                const size_t end = (t == 3) ? v.size() : begin + chunk;
                for (size_t i = begin; i < end; ++i) mgr.store(v[i]);
            });
        }
        for (auto& th : threads) th.join();

        CHECK(mgr.framesProcessed() == 600);
        auto events = mgr.finish();
        auto results = mgr.orderedResults();

        bool inOrder = results.size() == 600;
        for (size_t i = 0; i < results.size(); ++i) inOrder = inOrder && results[i].frameId == i;
        CHECK(inOrder);

        CHECK(events.size() == refEvents.size());
        for (size_t i = 0; i < std::min(events.size(), refEvents.size()); ++i) {
            CHECK_NEAR(events[i].startSec, refEvents[i].startSec, 1e-9);
            CHECK_NEAR(events[i].endSec, refEvents[i].endSec, 1e-9);
            CHECK_NEAR(events[i].score, refEvents[i].score, 1e-9);
        }
    }

    // gaps (e.g. after a cancel) must not lose the frames that did arrive
    {
        ResultManager mgr{EventDetector(c)};
        mgr.store(make(0, 0));
        mgr.store(make(1, 0));
        mgr.store(make(5, 0));        // 2,3,4 never arrive
        mgr.finish();
        CHECK(mgr.orderedResults().size() == 3);
    }

    return finishTest("test_resultmanager");
}
