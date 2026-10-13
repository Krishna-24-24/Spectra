#pragma once
//
// EventDetector
// -------------
// Turns the stream of per-frame measurements into a short list of events.
//
// Rules for ONE frame (checked in this order):
//   Sudden Visual Change : average brightness jumped by >= 40 levels (flash, lights on/off)
//   Scene Change         : >= 50% of the pixels changed AND the brightness histogram moved
//   High Activity        : >= 10% of the pixels changed
//   Significant Motion   : >=  1% of the pixels changed
//
// Grouping (so continuous movement is ONE event, not hundreds):
//   detections of the same kind that are less than 0.75 s apart are merged.
//   Tiny motion bursts (fewer than 3 frames) are ignored as noise.
//
// Frames MUST be fed in frame order (ResultManager guarantees this).

#include "Types.hpp"

#include <vector>

namespace spectra {

class EventDetector {
public:
    explicit EventDetector(DetectorConfig config = DetectorConfig());

    void onFrame(const FrameResult& frame);

    // Close anything still open and return all events sorted by start time.
    std::vector<EventInfo> finish();

    // Events closed so far + events currently open (used for the live counter).
    std::size_t eventCount() const;

private:
    enum Family { Motion = 0, Scene = 1, Sudden = 2, FamilyCount = 3 };

    struct Open {
        bool active = false;
        double start = 0.0;
        double last = 0.0;
        int frames = 0;
        int highFrames = 0;
        double peakRatio = 0.0;
        double peakHistogram = 0.0;
        double peakBrightness = 0.0;
    };

    void closeStale(double now);
    void close(Family family);

    DetectorConfig config_;
    Open open_[FamilyCount];
    std::vector<EventInfo> closed_;
};

} // namespace spectra
