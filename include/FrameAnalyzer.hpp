#pragma once
//
// FrameAnalyzer
// -------------
// The "computer vision" part. No machine learning - only simple, explainable
// image operations. For one frame it does:
//
//   1. shrink the frame (if it is very large) so analysis stays fast
//   2. convert to grayscale          (colour is not needed to see change)
//   3. Gaussian blur                 (hides camera noise / compression noise)
//   4. absolute difference with the previous frame
//   5. threshold                     (a pixel "changed" if it differs by > 25)
//   6. changed-pixel ratio           (how much of the picture changed)
//   7. largest changed region        (contour analysis of the changed area)
//   8. histogram distance + average brightness change  (for scene changes / flashes)
//
// analyze() only reads its input and never touches shared data, so many worker
// threads can call it at the same time without any locking.

#include "Types.hpp"

namespace spectra {

class FrameAnalyzer {
public:
    explicit FrameAnalyzer(AnalyzerConfig config = AnalyzerConfig());

    FrameResult analyze(const FrameTask& task) const;

    const AnalyzerConfig& config() const { return config_; }

private:
    cv::Mat prepare(const cv::Mat& frame) const;   // shrink + gray + blur

    AnalyzerConfig config_;
};

} // namespace spectra
