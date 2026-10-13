#include "ApplicationController.hpp"

#include "ReportWriter.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <exception>

namespace {

std::string toStd(const QString& s)
{
    return s.toLocal8Bit().toStdString();
}

} // namespace

ApplicationController::ApplicationController(QObject* parent)
    : QObject(parent)
{
    // Custom types must be registered before they can travel inside queued signals.
    qRegisterMetaType<spectra::ProgressInfo>("spectra::ProgressInfo");
    qRegisterMetaType<AnalysisOutcome>("AnalysisOutcome");
    qRegisterMetaType<BenchmarkOutcome>("BenchmarkOutcome");
}

ApplicationController::~ApplicationController()
{
    cancel_ = true;     // never leave a thread running when the window closes
    joinWorker();
}

void ApplicationController::joinWorker()
{
    if (worker_.joinable()) {
        worker_.join();
    }
}

void ApplicationController::setBusy(bool busy)
{
    busy_ = busy;
    emit busyChanged(busy);
}

std::string ApplicationController::baselineKey(const std::string& path, long long maxFrames)
{
    return path + "|" + std::to_string(maxFrames);
}

QString ApplicationController::outputDirectory() const
{
    const QString dir = QDir(QCoreApplication::applicationDirPath()).filePath("output");
    QDir().mkpath(dir);
    return dir;
}

bool ApplicationController::loadVideo(const QString& path, spectra::VideoMetadata& meta, QString& error)
{
    const spectra::ProbeResult probe = spectra::VideoProcessor::probe(toStd(path));
    if (!probe.ok) {
        error = QString::fromStdString(probe.error);
        return false;
    }
    meta = probe.meta;
    return true;
}

void ApplicationController::stopWork()
{
    if (busy_) {
        cancel_ = true;
        emit statusMessage("Stopping... finishing the frames already in progress.");
    }
}

// ---------------------------------------------------------------------------
// Analysis
// ---------------------------------------------------------------------------
void ApplicationController::startAnalysis(const spectra::AnalysisSettings& settingsIn, bool saveProcessedVideo)
{
    if (busy_) {
        emit failed("An analysis or benchmark is already running.");
        return;
    }
    joinWorker();
    cancel_ = false;
    setBusy(true);
    emit statusMessage("Analysing...");

    const spectra::AnalysisSettings settings = settingsIn;
    const QString outDir = outputDirectory();

    worker_ = std::thread([this, settings, saveProcessedVideo, outDir]() {
        AnalysisOutcome outcome;
        try {
            spectra::RunHooks hooks;
            hooks.cancel = &cancel_;
            hooks.onProgress = [this](const spectra::ProgressInfo& p) { emit analysisProgress(p); };
            outcome.result = spectra::VideoProcessor().run(settings, hooks);

            if (outcome.result.ok && !outcome.result.cancelled) {
                const QString stem = QFileInfo(QString::fromLocal8Bit(settings.videoPath.c_str())).completeBaseName();
                std::string err;

                const QString csv = QDir(outDir).filePath(stem + "_events.csv");
                if (spectra::writeEventReport(toStd(csv), outcome.result.events, err)) {
                    outcome.eventsCsvPath = csv;
                } else {
                    outcome.note += QString::fromStdString(err) + " ";
                }

                if (saveProcessedVideo) {
                    emit statusMessage("Writing the processed video...");
                    const QString avi = QDir(outDir).filePath(stem + "_processed.avi");
                    std::string verr;
                    const bool ok = spectra::writeProcessedVideo(
                        settings.videoPath, toStd(avi), outcome.result.events, &cancel_,
                        [this](double f) { emit statusMessage(QString("Writing the processed video... %1%").arg(int(f * 100))); },
                        verr);
                    if (ok) {
                        outcome.processedVideoPath = avi;
                    } else if (!cancel_) {
                        outcome.note += QString::fromStdString(verr) + " ";
                    }
                }
            }
        } catch (const std::exception& e) {
            outcome.result.ok = false;
            outcome.result.error = std::string("Unexpected error: ") + e.what();
        } catch (...) {
            outcome.result.ok = false;
            outcome.result.error = "Unexpected error while analysing the video.";
        }
        if (cancel_ && outcome.result.ok) {
            outcome.result.cancelled = true;
        }

        // Finish in the GUI thread: baselines + busy flag are only touched there.
        QMetaObject::invokeMethod(this, [this, outcome, settings]() mutable {
            const std::string key = baselineKey(settings.videoPath, settings.maxFrames);
            if (outcome.result.ok && !outcome.result.cancelled && !settings.useThreadPool) {
                baselines_[key] = outcome.result.perf.wallSeconds;   // a true 1-thread run
            }
            const auto it = baselines_.find(key);
            outcome.baselineSec = (it != baselines_.end()) ? it->second : 0.0;
            setBusy(false);
            emit analysisFinished(outcome);
        }, Qt::QueuedConnection);
    });
}

// ---------------------------------------------------------------------------
// Benchmark
// ---------------------------------------------------------------------------
void ApplicationController::startBenchmark(const spectra::BenchmarkOptions& options)
{
    if (busy_) {
        emit failed("An analysis or benchmark is already running.");
        return;
    }
    joinWorker();
    cancel_ = false;
    setBusy(true);
    emit statusMessage("Running benchmark...");

    const QString outDir = outputDirectory();

    worker_ = std::thread([this, options, outDir]() {
        BenchmarkOutcome outcome;
        try {
            spectra::BenchmarkHooks hooks;
            hooks.cancel = &cancel_;
            hooks.onStep = [this](int done, int total, const std::string& label) {
                emit benchmarkStep(done, total, QString::fromStdString(label));
            };
            hooks.onProgress = [this](const spectra::ProgressInfo& p) { emit analysisProgress(p); };
            outcome.result = spectra::BenchmarkManager().run(options, hooks);

            if (outcome.result.ok && !outcome.result.cancelled && !outcome.result.rows.empty()) {
                const QString stem = QFileInfo(QString::fromLocal8Bit(options.videoPath.c_str())).completeBaseName();
                const QString csv = QDir(outDir).filePath(stem + "_benchmark.csv");
                std::string err;
                if (spectra::writeBenchmarkReport(toStd(csv), outcome.result.rows, err)) {
                    outcome.csvPath = csv;
                }
            }
        } catch (const std::exception& e) {
            outcome.result.ok = false;
            outcome.result.error = std::string("Unexpected error: ") + e.what();
        } catch (...) {
            outcome.result.ok = false;
            outcome.result.error = "Unexpected error during the benchmark.";
        }

        QMetaObject::invokeMethod(this, [this, outcome, options]() {
            if (outcome.result.ok && !outcome.result.cancelled) {
                for (const auto& row : outcome.result.rows) {
                    if (row.threads == 1) {
                        baselines_[baselineKey(options.videoPath, options.maxFrames)] = row.timeSec;
                    }
                }
            }
            setBusy(false);
            emit benchmarkFinished(outcome);
        }, Qt::QueuedConnection);
    });
}
