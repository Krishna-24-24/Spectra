// spectra_cli - run the analysis engine from a console (no GUI needed).
//
//   spectra_cli <video> [--threads N] [--single] [--max-frames N]
//                       [--no-motion] [--no-scene] [--no-activity]
//                       [--csv events.csv] [--processed out.avi]
//   spectra_cli <video> --benchmark [--counts 1,2,4,8] [--repeats R] [--csv bench.csv]
//
// Handy for quick checks, for scripts, and as a fallback if the GUI cannot start.

#include "BenchmarkManager.hpp"
#include "PerformanceMonitor.hpp"
#include "ReportWriter.hpp"
#include "VideoProcessor.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace spectra;

static void usage()
{
    std::cout <<
        "Usage:\n"
        "  spectra_cli <video> [--threads N] [--single] [--max-frames N]\n"
        "                      [--no-motion] [--no-scene] [--no-activity]\n"
        "                      [--csv events.csv] [--processed out.avi]\n"
        "  spectra_cli <video> --benchmark [--counts 1,2,4,8] [--repeats R] [--csv bench.csv]\n";
}

static std::vector<int> parseCounts(const std::string& text)
{
    std::vector<int> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(std::atoi(item.c_str()));
        }
    }
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage();
        return 2;
    }

    std::string video = argv[1];
    int threads = 4;
    bool single = false, benchmark = false;
    long long maxFrames = 0;
    int repeats = 2;
    std::vector<int> counts{1, 2, 4, 8};
    std::string csv, processed;
    DetectorConfig det;

    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--threads") threads = std::atoi(next("--threads").c_str());
        else if (a == "--single") single = true;
        else if (a == "--benchmark") benchmark = true;
        else if (a == "--max-frames") maxFrames = std::atoll(next("--max-frames").c_str());
        else if (a == "--repeats") repeats = std::atoi(next("--repeats").c_str());
        else if (a == "--counts") counts = parseCounts(next("--counts"));
        else if (a == "--csv") csv = next("--csv");
        else if (a == "--processed") processed = next("--processed");
        else if (a == "--no-motion") det.detectMotion = false;
        else if (a == "--no-scene") det.detectSceneChange = false;
        else if (a == "--no-activity") det.detectActivity = false;
        else { usage(); return 2; }
    }

    if (benchmark) {
        BenchmarkOptions opt;
        opt.videoPath = video;
        opt.threadCounts = counts;
        opt.repeats = repeats;
        opt.maxFrames = maxFrames;
        opt.detector = det;
        BenchmarkHooks hooks;
        hooks.onStep = [](int done, int total, const std::string& label) {
            std::cout << "  [" << done << "/" << total << "] " << label << " done\n" << std::flush;
        };
        std::cout << "Benchmarking " << video << " ...\n";
        BenchmarkResult r = BenchmarkManager().run(opt, hooks);
        if (!r.ok) {
            std::cerr << "Benchmark failed: " << r.error << "\n";
            return 1;
        }
        std::printf("\n%-8s %-10s %-10s %-9s %-11s %-8s %-9s\n", "Threads", "Time(s)", "FPS", "Speedup", "Efficiency", "CPU%", "Mem(MB)");
        for (const auto& row : r.rows) {
            std::printf("%-8d %-10.3f %-10.1f %-9.2f %-12.1f %-8.1f %-9.1f\n", row.threads, row.timeSec, row.fps,
                        row.speedup, row.efficiency, row.cpuPercent, row.memoryMB);
        }
        std::cout << "\nSummary:\n";
        for (const auto& line : r.interpretation) {
            std::cout << " - " << line << "\n";
        }
        if (!csv.empty()) {
            std::string err;
            if (writeBenchmarkReport(csv, r.rows, err)) std::cout << "\nSaved " << csv << "\n";
            else std::cerr << err << "\n";
        }
        return 0;
    }

    AnalysisSettings s;
    s.videoPath = video;
    s.workers = single ? 1 : threads;
    s.useThreadPool = !single;
    s.maxFrames = maxFrames;
    s.detector = det;

    RunHooks hooks;
    hooks.onProgress = [](const ProgressInfo& p) {
        std::fprintf(stderr, "\r  frames %llu / %lld   %.1f fps   events %zu   ",
                     static_cast<unsigned long long>(p.framesProcessed), p.totalFrames, p.fps, p.eventsFound);
    };

    std::cout << "Analysing " << video << " with " << (single ? std::string("1 thread (single-threaded loop)")
                                                             : std::to_string(threads) + " worker threads") << " ...\n";
    RunResult r = VideoProcessor().run(s, hooks);
    std::fprintf(stderr, "\n");
    if (!r.ok) {
        std::cerr << "Error: " << r.error << "\n";
        return 1;
    }
    std::printf("Video: %s  %dx%d  %.2f fps  %lld frames\n", r.meta.fileName.c_str(), r.meta.width, r.meta.height,
                r.meta.fps, r.meta.frameCount);
    std::printf("Processed %llu frames in %.3f s  (%.1f fps)   CPU %.1f%%   memory %.1f MB\n",
                static_cast<unsigned long long>(r.framesProcessed), r.perf.wallSeconds, r.fps, r.perf.cpuPercent, r.perf.peakMemoryMB);
    std::printf("\n%-14s %-22s %-7s %-8s\n", "Timestamp", "Event", "Score", "Duration");
    for (const auto& e : r.events) {
        std::printf("%-14s %-22s %-7.2f %-6.2fs\n", formatTimestamp(e.startSec, true).c_str(), eventTypeName(e.type), e.score, e.duration());
    }
    std::printf("\n%zu event(s) found.\n", r.events.size());
    for (const auto& w : r.warnings) {
        std::cout << "Warning: " << w << "\n";
    }

    std::string err;
    if (!csv.empty()) {
        if (writeEventReport(csv, r.events, err)) std::cout << "Saved " << csv << "\n";
        else std::cerr << err << "\n";
    }
    if (!processed.empty()) {
        std::cout << "Writing processed video ...\n";
        if (writeProcessedVideo(video, processed, r.events, nullptr, nullptr, err)) std::cout << "Saved " << processed << "\n";
        else std::cerr << err << "\n";
    }
    return 0;
}
