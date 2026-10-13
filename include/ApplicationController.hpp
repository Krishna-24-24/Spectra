#pragma once
//
// ApplicationController
// ---------------------
// The bridge between the window and the processing engine.
//
//   * The engine runs on a background std::thread, so the window never freezes.
//   * Results come back as Qt signals; because the receiver lives in the GUI thread,
//     Qt delivers them safely in the GUI thread ("queued connection").
//   * stopWork() sets an atomic flag. The engine checks it, stops its threads
//     cleanly and returns - nothing is killed forcefully.

#include "BenchmarkManager.hpp"
#include "Types.hpp"
#include "VideoProcessor.hpp"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>
#include <map>
#include <string>
#include <thread>

struct AnalysisOutcome {
    spectra::RunResult result;
    QString eventsCsvPath;        // empty if nothing was saved
    QString processedVideoPath;   // empty if not requested / failed
    double baselineSec = 0.0;     // known single-thread time for this video (0 = unknown)
    QString note;                 // e.g. why the processed video was not written
};

struct BenchmarkOutcome {
    spectra::BenchmarkResult result;
    QString csvPath;
};

Q_DECLARE_METATYPE(spectra::ProgressInfo)
Q_DECLARE_METATYPE(AnalysisOutcome)
Q_DECLARE_METATYPE(BenchmarkOutcome)

class ApplicationController : public QObject {
    Q_OBJECT
public:
    explicit ApplicationController(QObject* parent = nullptr);
    ~ApplicationController() override;

    // Checks the file (fast) and returns its properties.
    bool loadVideo(const QString& path, spectra::VideoMetadata& meta, QString& error);

    bool isBusy() const { return busy_.load(); }
    QString outputDirectory() const;

    void startAnalysis(const spectra::AnalysisSettings& settings, bool saveProcessedVideo);
    void startBenchmark(const spectra::BenchmarkOptions& options);
    void stopWork();

signals:
    void busyChanged(bool busy);
    void statusMessage(const QString& text);
    void analysisProgress(const spectra::ProgressInfo& info);
    void analysisFinished(const AnalysisOutcome& outcome);
    void benchmarkStep(int done, int total, const QString& label);
    void benchmarkFinished(const BenchmarkOutcome& outcome);
    void failed(const QString& message);

private:
    void joinWorker();
    void setBusy(bool busy);
    static std::string baselineKey(const std::string& path, long long maxFrames);

    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> busy_{false};
    std::map<std::string, double> baselines_;   // only touched in the GUI thread
};
