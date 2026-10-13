#pragma once
//
// ReportWriter
// ------------
// Writes the two CSV reports and the processed (annotated) video.
//
//   events.csv      timestamp,event_type,score,duration
//   benchmark.csv   threads,time,fps,speedup,efficiency,cpu_percent,memory_mb
//   processed.avi   the original video with a red frame + label while an event is happening

#include "Types.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace spectra {

// "00:02:31" (or "00:02:31.250" with milliseconds)
std::string formatTimestamp(double seconds, bool withMilliseconds = false);

bool writeEventReport(const std::string& path, const std::vector<EventInfo>& events, std::string& error);

bool writeBenchmarkReport(const std::string& path, const std::vector<BenchmarkRow>& rows, std::string& error);

// Re-reads the input video and writes an MJPG .avi with the events drawn on top.
// onProgress receives 0..1. Returns false (and fills error) on failure or when cancelled.
bool writeProcessedVideo(const std::string& inputPath,
                         const std::string& outputPath,
                         const std::vector<EventInfo>& events,
                         const std::atomic<bool>* cancel,
                         const std::function<void(double)>& onProgress,
                         std::string& error);

} // namespace spectra
