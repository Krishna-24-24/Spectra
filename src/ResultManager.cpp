#include "ResultManager.hpp"

namespace spectra {

ResultManager::ResultManager(EventDetector detector)
    : detector_(std::move(detector))
{
}

void ResultManager::store(const FrameResult& result)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pending_[result.frameId] = result;
    processed_.fetch_add(1, std::memory_order_relaxed);
    drainLocked();
    eventsFound_.store(detector_.eventCount(), std::memory_order_relaxed);
}

void ResultManager::drainLocked()
{
    // Hand over every result that is next in line.
    for (;;) {
        auto it = pending_.find(nextId_);
        if (it == pending_.end()) {
            break;
        }
        detector_.onFrame(it->second);
        ordered_.push_back(it->second);
        pending_.erase(it);
        ++nextId_;
    }
}

std::vector<EventInfo> ResultManager::finish()
{
    std::lock_guard<std::mutex> lock(mutex_);
    // After a cancel there can be gaps (frames that were never analysed).
    // Hand over whatever is left, still in ID order.
    for (auto& entry : pending_) {
        detector_.onFrame(entry.second);
        ordered_.push_back(entry.second);
    }
    pending_.clear();
    std::vector<EventInfo> events = detector_.finish();
    eventsFound_.store(events.size(), std::memory_order_relaxed);
    return events;
}

std::vector<FrameResult> ResultManager::orderedResults() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return ordered_;
}

} // namespace spectra
