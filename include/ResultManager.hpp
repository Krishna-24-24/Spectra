#pragma once
//
// ResultManager
// -------------
// Collects the results coming back from the worker threads.
//
// Problem: worker 3 may finish frame 40 before worker 1 finishes frame 39.
// Solution: every result has a frame ID. Results are parked in a map and are
// only handed to the EventDetector when the NEXT expected ID is available, so
// the detector always sees frames in the original order 0,1,2,3,...
// (one mutex protects all of this).

#include "EventDetector.hpp"
#include "Types.hpp"

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace spectra {

class ResultManager {
public:
    explicit ResultManager(EventDetector detector);

    // Thread-safe. Called by workers (or by the single-threaded loop).
    void store(const FrameResult& result);

    // Results handed to the detector so far (counts every frame, in any order).
    std::uint64_t framesProcessed() const { return processed_.load(std::memory_order_relaxed); }

    // Events found so far (closed + currently open). Cheap, for the live counter.
    std::size_t eventsFound() const { return eventsFound_.load(std::memory_order_relaxed); }

    // Call after every worker has finished. Returns the final, ordered events.
    std::vector<EventInfo> finish();

    // Every result in frame order (valid after finish()).
    std::vector<FrameResult> orderedResults() const;

private:
    void drainLocked();

    mutable std::mutex mutex_;
    std::map<std::uint64_t, FrameResult> pending_;   // finished but not yet in order
    std::vector<FrameResult> ordered_;
    std::uint64_t nextId_ = 0;
    EventDetector detector_;
    std::atomic<std::uint64_t> processed_{0};
    std::atomic<std::size_t> eventsFound_{0};
};

} // namespace spectra
