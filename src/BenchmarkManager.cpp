#include "BenchmarkManager.hpp"

#include "PerformanceMonitor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace spectra {

namespace {

std::string fmt(const char* format, double a)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), format, a);
    return buf;
}

std::string fmt2(const char* format, double a, double b)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), format, a, b);
    return buf;
}

const BenchmarkRow* findRow(const std::vector<BenchmarkRow>& rows, int threads)
{
    for (const auto& r : rows) {
        if (r.threads == threads) {
            return &r;
        }
    }
    return nullptr;
}

} // namespace

void BenchmarkManager::computeDerived(std::vector<BenchmarkRow>& rows)
{
    const BenchmarkRow* base = findRow(rows, 1);
    if (!base || base->timeSec <= 0.0) {
        return;
    }
    const double baseTime = base->timeSec;
    for (auto& r : rows) {
        r.speedup = r.timeSec > 0.0 ? baseTime / r.timeSec : 0.0;
        r.efficiency = r.threads > 0 ? r.speedup / r.threads * 100.0 : 0.0;
    }
}

BenchmarkResult BenchmarkManager::run(const BenchmarkOptions& options, const BenchmarkHooks& hooks) const
{
    BenchmarkResult result;

    auto cancelled = [&hooks]() { return hooks.cancel && hooks.cancel->load(std::memory_order_relaxed); };

    // ---- clean up the requested thread counts ------------------------------
    std::set<int> counts(options.threadCounts.begin(), options.threadCounts.end());
    for (int c : counts) {
        if (c < 1 || c > kMaxWorkers) {
            result.error = "Invalid thread count in the benchmark list: " + std::to_string(c);
            return result;
        }
    }
    counts.insert(1);   // the single-thread baseline is always needed for speedup
    if (counts.size() < 2) {
        result.error = "Select at least one thread count besides 1 for the benchmark.";
        return result;
    }
    const int repeats = std::max(1, std::min(options.repeats, 10));

    ProbeResult probe = VideoProcessor::probe(options.videoPath);
    if (!probe.ok) {
        result.error = probe.error;
        return result;
    }
    result.meta = probe.meta;

    VideoProcessor processor;
    auto makeSettings = [&](int threads, long long maxFrames) {
        AnalysisSettings s;
        s.videoPath = options.videoPath;
        s.workers = threads;
        s.useThreadPool = threads > 1;
        s.maxFrames = maxFrames;
        s.analyzer = options.analyzer;
        s.detector = options.detector;
        return s;
    };
    RunHooks runHooks;
    runHooks.cancel = hooks.cancel;
    runHooks.onProgress = hooks.onProgress;

    const int totalRuns = static_cast<int>(counts.size()) * repeats + 1;   // +1 warm-up
    int doneRuns = 0;

    // ---- warm-up (not recorded) --------------------------------------------
    {
        RunResult warm = processor.run(makeSettings(1, 300), RunHooks{nullptr, hooks.cancel, 100});
        if (!warm.ok && !warm.cancelled) {
            result.error = warm.error;
            return result;
        }
        ++doneRuns;
        if (hooks.onStep) {
            hooks.onStep(doneRuns, totalRuns, "warm-up");
        }
    }

    // ---- measured runs --------------------------------------------------------
    for (int threads : counts) {
        double sumTime = 0, sumCpu = 0, maxMem = 0;
        std::uint64_t frames = 0;
        int goodRuns = 0;
        for (int rep = 1; rep <= repeats; ++rep) {
            if (cancelled()) {
                result.cancelled = true;
                break;
            }
            RunResult r = processor.run(makeSettings(threads, options.maxFrames), runHooks);
            if (r.cancelled) {
                result.cancelled = true;
                break;
            }
            if (!r.ok) {
                result.error = r.error;
                return result;
            }
            sumTime += r.perf.wallSeconds;
            sumCpu += r.perf.cpuPercent;
            maxMem = std::max(maxMem, r.perf.peakMemoryMB);
            frames = r.framesProcessed;
            ++goodRuns;
            ++doneRuns;
            if (hooks.onStep) {
                hooks.onStep(doneRuns, totalRuns,
                             std::to_string(threads) + (threads == 1 ? " thread" : " threads")
                                 + " (run " + std::to_string(rep) + "/" + std::to_string(repeats) + ")");
            }
        }
        if (result.cancelled) {
            break;
        }
        if (goodRuns > 0) {
            BenchmarkRow row;
            row.threads = threads;
            row.timeSec = sumTime / goodRuns;
            row.fps = row.timeSec > 0.0 ? frames / row.timeSec : 0.0;
            row.cpuPercent = sumCpu / goodRuns;
            row.memoryMB = maxMem;
            result.rows.push_back(row);
        }
    }

    computeDerived(result.rows);
    result.interpretation = interpret(result.rows);
    result.ok = true;
    return result;
}

std::vector<std::string> BenchmarkManager::interpret(const std::vector<BenchmarkRow>& rowsIn)
{
    std::vector<std::string> lines;
    std::vector<BenchmarkRow> rows = rowsIn;
    std::sort(rows.begin(), rows.end(), [](const BenchmarkRow& a, const BenchmarkRow& b) { return a.threads < b.threads; });

    const BenchmarkRow* base = findRow(rows, 1);
    if (!base || rows.size() < 2) {
        lines.push_back("Not enough benchmark data to interpret yet.");
        return lines;
    }

    // Best configuration.
    const BenchmarkRow* best = base;
    for (const auto& r : rows) {
        if (r.timeSec < best->timeSec) {
            best = &r;
        }
    }

    if (best->threads == 1) {
        lines.push_back("Using more threads did not make the analysis faster on this video. The work per frame is small "
                        "compared with the time spent reading/decoding frames and coordinating threads.");
    } else {
        const double saved = (1.0 - best->timeSec / base->timeSec) * 100.0;
        lines.push_back(fmt2("Processing time dropped from %.2f s (1 thread) to %.2f s ", base->timeSec, best->timeSec)
                        + "with " + std::to_string(best->threads) + " threads, "
                        + fmt2("a speedup of %.2fx (%.0f%% less time).", best->speedup, saved));
    }

    // Gains between neighbouring thread counts.
    for (std::size_t i = 1; i < rows.size(); ++i) {
        const BenchmarkRow& prev = rows[i - 1];
        const BenchmarkRow& cur = rows[i];
        if (prev.timeSec <= 0.0) {
            continue;
        }
        const double gain = (1.0 - cur.timeSec / prev.timeSec) * 100.0;
        const std::string step = "Going from " + std::to_string(prev.threads) + " to " + std::to_string(cur.threads)
                                 + " threads ";
        if (gain >= 25.0) {
            lines.push_back(step + fmt("reduced the time by a further %.0f%% - a large gain.", gain));
        } else if (gain >= 5.0) {
            lines.push_back(step + fmt("reduced the time by only %.0f%% - the gain is getting smaller.", gain));
        } else if (gain >= -5.0) {
            lines.push_back(step + "made almost no difference - extra threads no longer help.");
        } else {
            lines.push_back(step + fmt("made it %.0f%% slower - the cost of coordinating more threads outweighed the benefit.", -gain));
        }
    }

    // Efficiency trend.
    const BenchmarkRow& last = rows.back();
    bool falling = true;
    for (std::size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].efficiency > rows[i - 1].efficiency + 1.0) {
            falling = false;
        }
    }
    if (falling) {
        lines.push_back("Parallel efficiency decreased as the thread count increased (100% at 1 thread, "
                        + fmt("%.0f", last.efficiency) + "% at " + std::to_string(last.threads) + " threads).");
    } else {
        lines.push_back("Parallel efficiency did not fall steadily; run-to-run noise on this machine is visible in the results.");
    }

    lines.push_back("This is the expected pattern: reading frames and coordinating threads are not parallel (Amdahl's law), "
                    "and this computer has " + std::to_string(PerformanceMonitor::hardwareThreads())
                    + " logical processors, so gains flatten around that number.");
    return lines;
}

} // namespace spectra
