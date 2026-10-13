#include "PerformanceMonitor.hpp"

#include <algorithm>
#include <thread>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#else
#  include <sys/resource.h>
#  include <unistd.h>
#  include <cstdio>
#endif

namespace spectra {

namespace {

// Total CPU time (user + kernel) used by this process so far, in seconds.
double processCpuSeconds()
{
#ifdef _WIN32
    FILETIME creation, exitTime, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        return 0.0;
    }
    auto toSeconds = [](const FILETIME& ft) {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return static_cast<double>(u.QuadPart) / 1.0e7;   // 100-nanosecond units
    };
    return toSeconds(kernel) + toSeconds(user);
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return 0.0;
    }
    return ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1.0e6
         + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1.0e6;
#endif
}

// Memory currently used by this process, in kilobytes.
long long processMemoryKB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return static_cast<long long>(pmc.WorkingSetSize / 1024);
    }
    return 0;
#else
    long long pages = 0, resident = 0;
    FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) {
        return 0;
    }
    if (std::fscanf(f, "%lld %lld", &pages, &resident) != 2) {
        resident = 0;
    }
    std::fclose(f);
    return resident * (sysconf(_SC_PAGESIZE) / 1024);
#endif
}

} // namespace

unsigned PerformanceMonitor::hardwareThreads()
{
    unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : n;
}

void PerformanceMonitor::start()
{
    peakKB_.store(processMemoryKB());
    cpuSecondsAtStart_ = processCpuSeconds();
    startTime_ = std::chrono::steady_clock::now();
}

void PerformanceMonitor::sampleMemory()
{
    const long long now = processMemoryKB();
    long long peak = peakKB_.load();
    while (now > peak && !peakKB_.compare_exchange_weak(peak, now)) {
        // peak was reloaded by compare_exchange_weak; loop again
    }
}

double PerformanceMonitor::elapsedSeconds() const
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime_).count();
}

PerfSnapshot PerformanceMonitor::stop()
{
    sampleMemory();
    PerfSnapshot snap;
    snap.wallSeconds = elapsedSeconds();
    const double cpuUsed = processCpuSeconds() - cpuSecondsAtStart_;
    if (snap.wallSeconds > 0.0) {
        snap.cpuPercent = cpuUsed / (snap.wallSeconds * hardwareThreads()) * 100.0;
        snap.cpuPercent = std::min(100.0, std::max(0.0, snap.cpuPercent));
    }
    snap.peakMemoryMB = peakKB_.load() / 1024.0;
    return snap;
}

} // namespace spectra
