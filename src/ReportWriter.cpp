#include "ReportWriter.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace spectra {

namespace {

bool prepareOutputFile(const std::string& path, std::ofstream& out, std::string& error)
{
    try {
        const std::filesystem::path p = std::filesystem::u8path(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        out.open(p, std::ios::out | std::ios::trunc);
    } catch (const std::exception& e) {
        error = std::string("Cannot create the output folder: ") + e.what();
        return false;
    }
    if (!out.is_open()) {
        error = "Cannot write the file (is it open in Excel?):\n" + path;
        return false;
    }
    return true;
}

} // namespace

std::string formatTimestamp(double seconds, bool withMilliseconds)
{
    if (!(seconds > 0.0)) {
        seconds = 0.0;
    }
    const long long totalMs = static_cast<long long>(std::llround(seconds * 1000.0));
    const long long totalSec = totalMs / 1000;
    const int ms = static_cast<int>(totalMs % 1000);
    const int h = static_cast<int>(totalSec / 3600);
    const int m = static_cast<int>((totalSec % 3600) / 60);
    const int s = static_cast<int>(totalSec % 60);
    char buf[64];
    if (withMilliseconds) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    }
    return buf;
}

bool writeEventReport(const std::string& path, const std::vector<EventInfo>& events, std::string& error)
{
    std::ofstream out;
    if (!prepareOutputFile(path, out, error)) {
        return false;
    }
    out << "timestamp,event_type,score,duration\n";
    for (const auto& e : events) {
        out << formatTimestamp(e.startSec, true) << ','
            << eventTypeName(e.type) << ','
            << std::fixed << std::setprecision(2) << e.score << ','
            << std::fixed << std::setprecision(2) << e.duration() << '\n';
    }
    out.flush();
    if (!out) {
        error = "Writing the event report failed.";
        return false;
    }
    return true;
}

bool writeBenchmarkReport(const std::string& path, const std::vector<BenchmarkRow>& rows, std::string& error)
{
    std::ofstream out;
    if (!prepareOutputFile(path, out, error)) {
        return false;
    }
    out << "threads,time,fps,speedup,efficiency,cpu_percent,memory_mb\n";
    for (const auto& r : rows) {
        out << r.threads << ','
            << std::fixed << std::setprecision(3) << r.timeSec << ','
            << std::fixed << std::setprecision(1) << r.fps << ','
            << std::fixed << std::setprecision(2) << r.speedup << ','
            << std::fixed << std::setprecision(1) << r.efficiency << ','
            << std::fixed << std::setprecision(1) << r.cpuPercent << ','
            << std::fixed << std::setprecision(1) << r.memoryMB << '\n';
    }
    out.flush();
    if (!out) {
        error = "Writing the benchmark report failed.";
        return false;
    }
    return true;
}

bool writeProcessedVideo(const std::string& inputPath,
                         const std::string& outputPath,
                         const std::vector<EventInfo>& events,
                         const std::atomic<bool>* cancel,
                         const std::function<void(double)>& onProgress,
                         std::string& error)
{
    cv::VideoCapture cap;
    if (!cap.open(inputPath) || !cap.isOpened()) {
        error = "Could not re-open the video to create the processed copy.";
        return false;
    }
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (!std::isfinite(fps) || fps < 1.0 || fps > 1000.0) {
        fps = 25.0;
    }
    const double total = cap.get(cv::CAP_PROP_FRAME_COUNT);

    try {
        const std::filesystem::path p = std::filesystem::u8path(outputPath);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
    } catch (const std::exception& e) {
        error = std::string("Cannot create the output folder: ") + e.what();
        return false;
    }

    cv::VideoWriter writer;
    bool opened = false;
    std::uint64_t index = 0;
    const int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');

    cv::Mat frame;
    while (cap.read(frame) && !frame.empty()) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            error = "Cancelled.";
            writer.release();
            return false;
        }

        // Keep the file size reasonable: never write wider than 1280 px.
        cv::Mat out = frame;
        if (out.cols > 1280) {
            const double scale = 1280.0 / out.cols;
            cv::resize(out, out, cv::Size(1280, std::max(1, static_cast<int>(out.rows * scale + 0.5))), 0, 0, cv::INTER_AREA);
        } else {
            out = frame.clone();
        }

        if (!opened) {
            // The built-in MJPEG backend needs no codec packs, so it works on every PC.
            opened = writer.open(outputPath, cv::CAP_OPENCV_MJPEG, fourcc, fps, out.size(), true);
            if (!opened) {
                opened = writer.open(outputPath, fourcc, fps, out.size(), true);
            }
            if (!opened) {
                error = "Could not create the processed video file:\n" + outputPath;
                return false;
            }
        }

        const double t = index / fps;
        const EventInfo* active = nullptr;
        for (const auto& e : events) {
            if (t >= e.startSec && t <= e.endSec) {
                active = &e;
                break;
            }
        }
        if (active) {
            const int border = std::max(4, out.cols / 120);
            cv::rectangle(out, cv::Point(0, 0), cv::Point(out.cols - 1, out.rows - 1), cv::Scalar(0, 0, 255), border);
            std::ostringstream label;
            label << eventTypeName(active->type) << "  " << std::fixed << std::setprecision(2) << active->score;
            const double fontScale = std::max(0.5, out.cols / 1000.0);
            int baseline = 0;
            const cv::Size ts = cv::getTextSize(label.str(), cv::FONT_HERSHEY_SIMPLEX, fontScale, 2, &baseline);
            cv::rectangle(out, cv::Point(border, border), cv::Point(border + ts.width + 16, border + ts.height + baseline + 14),
                          cv::Scalar(0, 0, 200), cv::FILLED);
            cv::putText(out, label.str(), cv::Point(border + 8, border + ts.height + 6),
                        cv::FONT_HERSHEY_SIMPLEX, fontScale, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
        }

        writer.write(out);
        ++index;
        if (onProgress && total > 0 && (index % 15 == 0)) {
            onProgress(std::min(1.0, index / total));
        }
    }

    writer.release();
    if (!opened) {
        error = "No frames could be read for the processed video.";
        return false;
    }
    if (onProgress) {
        onProgress(1.0);
    }
    return true;
}

} // namespace spectra
