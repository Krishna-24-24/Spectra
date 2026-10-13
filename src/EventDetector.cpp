#include "EventDetector.hpp"

#include <algorithm>
#include <cmath>

namespace spectra {

EventDetector::EventDetector(DetectorConfig config)
    : config_(config)
{
}

void EventDetector::onFrame(const FrameResult& f)
{
    const double t = f.timestampSec;
    closeStale(t);

    const double absBrightness = std::fabs(f.brightnessDelta);

    // Decide what this single frame looks like.
    enum class Label { None, Motion, High, Scene, Sudden };
    Label label = Label::None;

    if (config_.detectSceneChange && absBrightness >= config_.suddenBrightness) {
        label = Label::Sudden;
    } else if (config_.detectSceneChange
               && f.changedRatio >= config_.sceneChangeRatio
               && f.histogramDistance >= config_.sceneChangeHistogram) {
        label = Label::Scene;
    } else if (config_.detectActivity && f.changedRatio >= config_.activityThreshold) {
        label = Label::High;
    } else if (config_.detectMotion && f.changedRatio >= config_.motionThreshold) {
        label = Label::Motion;
    }

    if (label == Label::None) {
        return;
    }

    const Family family = (label == Label::Scene)  ? Scene
                        : (label == Label::Sudden) ? Sudden
                                                   : Motion;
    Open& o = open_[family];
    if (!o.active) {
        o = Open();
        o.active = true;
        o.start = t;
    }
    o.last = t;
    o.frames += 1;
    if (label == Label::High) {
        o.highFrames += 1;
    }
    o.peakRatio = std::max(o.peakRatio, f.changedRatio);
    o.peakHistogram = std::max(o.peakHistogram, f.histogramDistance);
    o.peakBrightness = std::max(o.peakBrightness, absBrightness);
}

void EventDetector::closeStale(double now)
{
    for (int i = 0; i < FamilyCount; ++i) {
        if (open_[i].active && (now - open_[i].last) > config_.mergeGapSec) {
            close(static_cast<Family>(i));
        }
    }
}

void EventDetector::close(Family family)
{
    Open& o = open_[family];
    if (!o.active) {
        return;
    }

    EventInfo e;
    e.startSec = o.start;
    e.endSec = o.last + config_.frameDurationSec;

    bool keep = true;
    switch (family) {
    case Motion: {
        keep = o.frames >= config_.minMotionFrames;
        // "High Activity" when at least half of the detected frames were intense.
        const bool mostlyHigh = config_.detectActivity && (o.highFrames * 2 >= o.frames);
        e.type = mostlyHigh ? EventType::HighActivity : EventType::SignificantMotion;
        e.score = std::min(1.0, o.peakRatio / config_.scoreFullScaleRatio);
        break;
    }
    case Scene:
        e.type = EventType::SceneChange;
        e.score = std::min(1.0, o.peakRatio);
        break;
    case Sudden:
        e.type = EventType::SuddenVisualChange;
        e.score = std::min(1.0, o.peakBrightness / 100.0);
        break;
    default:
        keep = false;
        break;
    }

    if (keep) {
        closed_.push_back(e);
    }
    o = Open();
}

std::vector<EventInfo> EventDetector::finish()
{
    for (int i = 0; i < FamilyCount; ++i) {
        close(static_cast<Family>(i));
    }
    std::vector<EventInfo> out = closed_;
    std::stable_sort(out.begin(), out.end(),
                     [](const EventInfo& a, const EventInfo& b) { return a.startSec < b.startSec; });
    return out;
}

std::size_t EventDetector::eventCount() const
{
    std::size_t n = closed_.size();
    for (int i = 0; i < FamilyCount; ++i) {
        if (open_[i].active) {
            ++n;
        }
    }
    return n;
}

} // namespace spectra
