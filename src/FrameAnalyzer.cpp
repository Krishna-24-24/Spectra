#include "FrameAnalyzer.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace spectra {

FrameAnalyzer::FrameAnalyzer(AnalyzerConfig config)
    : config_(config)
{
    // Gaussian blur needs an odd kernel size >= 1.
    if (config_.blurKernel < 1) {
        config_.blurKernel = 1;
    }
    if (config_.blurKernel % 2 == 0) {
        config_.blurKernel += 1;
    }
    if (config_.histogramBins < 2) {
        config_.histogramBins = 2;
    }
}

cv::Mat FrameAnalyzer::prepare(const cv::Mat& frame) const
{
    cv::Mat work = frame;

    // 1. Shrink big frames (e.g. 1920x1080 -> 640x360). Much faster, and the
    //    changes we look for are large enough to survive shrinking.
    if (config_.analysisWidth > 0 && work.cols > config_.analysisWidth) {
        const double scale = static_cast<double>(config_.analysisWidth) / work.cols;
        const int newHeight = std::max(1, static_cast<int>(work.rows * scale + 0.5));
        cv::Mat small;
        cv::resize(work, small, cv::Size(config_.analysisWidth, newHeight), 0, 0, cv::INTER_AREA);
        work = small;
    }

    // 2. Grayscale.
    cv::Mat gray;
    if (work.channels() == 3) {
        cv::cvtColor(work, gray, cv::COLOR_BGR2GRAY);
    } else if (work.channels() == 4) {
        cv::cvtColor(work, gray, cv::COLOR_BGRA2GRAY);
    } else {
        gray = work;
    }

    // 3. Gaussian blur.
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(config_.blurKernel, config_.blurKernel), 0);
    return blurred;
}

FrameResult FrameAnalyzer::analyze(const FrameTask& task) const
{
    FrameResult result;
    result.frameId = task.frameId;
    result.timestampSec = task.timestampSec;

    // The first frame has nothing to compare with.
    if (task.frame.empty() || task.previous.empty()) {
        return result;
    }

    cv::Mat current = prepare(task.frame);
    cv::Mat previous = prepare(task.previous);
    if (previous.size() != current.size()) {
        cv::resize(previous, previous, current.size(), 0, 0, cv::INTER_AREA);
    }

    const double totalPixels = static_cast<double>(current.total());
    if (totalPixels <= 0.0) {
        return result;
    }

    // 4 + 5. Difference and threshold.
    cv::Mat diff;
    cv::absdiff(current, previous, diff);
    cv::Mat mask;
    cv::threshold(diff, mask, config_.pixelDiffThreshold, 255, cv::THRESH_BINARY);

    // 6. How much of the picture changed?
    result.changedRatio = cv::countNonZero(mask) / totalPixels;

    // 7. Largest connected changed region (join nearby changed pixels first).
    if (result.changedRatio > 0.0) {
        cv::Mat dilated;
        cv::dilate(mask, dilated, cv::Mat(), cv::Point(-1, -1), 2);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(dilated, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        double largest = 0.0;
        for (const auto& c : contours) {
            largest = std::max(largest, cv::contourArea(c));
        }
        result.largestRegionRatio = std::min(1.0, largest / totalPixels);
    }

    // 8. Brightness histogram distance and average brightness change.
    const int bins = config_.histogramBins;
    const int channels[] = {0};
    const float range[] = {0.f, 256.f};
    const float* ranges[] = {range};
    cv::Mat histCur, histPrev;
    cv::calcHist(&current, 1, channels, cv::Mat(), histCur, 1, &bins, ranges);
    cv::calcHist(&previous, 1, channels, cv::Mat(), histPrev, 1, &bins, ranges);
    cv::normalize(histCur, histCur, 1.0, 0.0, cv::NORM_L1);
    cv::normalize(histPrev, histPrev, 1.0, 0.0, cv::NORM_L1);
    result.histogramDistance = std::min(1.0, std::max(0.0, cv::compareHist(histCur, histPrev, cv::HISTCMP_BHATTACHARYYA)));

    result.brightnessDelta = cv::mean(current)[0] - cv::mean(previous)[0];
    return result;
}

} // namespace spectra
