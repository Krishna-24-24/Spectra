#pragma once
//
// MainWindow - the whole user interface.
//
//   Left column  : 1 Video input   2 Player   3 Analysis settings   4 Live status
//   Right column : tabs  [Events]  [Performance & Benchmark]  [Graphs]
//
// The window only shows things and forwards button clicks to ApplicationController.
// All heavy work happens in the engine on other threads.

#include "ApplicationController.hpp"

#include <QMainWindow>
#include <QString>

class VideoPlayer;
class VideoView;
class ChartWidget;

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTextEdit;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Public so the automated GUI test can drive the window like a user would.
    bool openVideoFile(const QString& path);
    void startAnalysis();
    void startBenchmark();
    ApplicationController* controller() { return controller_; }
    QTableWidget* eventTable() { return eventTable_; }
    QTableWidget* benchmarkTable() { return benchmarkTable_; }
    VideoPlayer* player() { return player_; }
    ChartWidget* chart(int index) { return charts_[index]; }
    void setDialogsEnabled(bool enabled) { dialogsEnabled_ = enabled; }
    void showTab(int index);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onBrowse();
    void onPlayPause();
    void onPlayerPosition(double seconds);
    void onPlaybackState(bool playing);
    void onSliderChanged(int value);
    void onEventClicked(int row, int column);
    void onBusyChanged(bool busy);
    void onProgress(const spectra::ProgressInfo& info);
    void onAnalysisFinished(const AnalysisOutcome& outcome);
    void onBenchmarkStep(int done, int total, const QString& label);
    void onBenchmarkFinished(const BenchmarkOutcome& outcome);
    void onFailed(const QString& message);
    void onModeChanged();
    void onOpenOutputFolder();

private:
    void buildUi();
    QWidget* buildLeftColumn();
    QWidget* buildRightColumn();
    QWidget* buildEventsTab();
    QWidget* buildPerformanceTab();
    QWidget* buildGraphsTab();

    void showError(const QString& message);
    void setMetadata(const spectra::VideoMetadata& meta, qint64 fileBytes);
    void clearResults();
    spectra::DetectorConfig detectorFromUi() const;
    void updateEventTable(const std::vector<spectra::EventInfo>& events);
    void updatePerformancePanel(const AnalysisOutcome& outcome);
    void updateBenchmarkViews(const std::vector<spectra::BenchmarkRow>& rows);
    void updateButtons();

    ApplicationController* controller_ = nullptr;
    VideoPlayer* player_ = nullptr;
    bool dialogsEnabled_ = true;
    bool updatingSlider_ = false;
    bool busy_ = false;
    bool videoLoaded_ = false;
    QString videoPath_;
    spectra::VideoMetadata meta_;

    QTabWidget* tabs_ = nullptr;
    // 1 video input
    QPushButton* browseButton_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* metaLabel_ = nullptr;
    // 2 player
    VideoView* videoView_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    // 3 settings
    QRadioButton* singleRadio_ = nullptr;
    QRadioButton* multiRadio_ = nullptr;
    QComboBox* workersCombo_ = nullptr;
    QCheckBox* motionCheck_ = nullptr;
    QCheckBox* sceneCheck_ = nullptr;
    QCheckBox* activityCheck_ = nullptr;
    QCheckBox* processedCheck_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopAnalysisButton_ = nullptr;
    // 4 live status
    QProgressBar* progressBar_ = nullptr;
    QLabel* framesLabel_ = nullptr;
    QLabel* fpsLabel_ = nullptr;
    QLabel* workersLabel_ = nullptr;
    QLabel* eventsLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    // events tab
    QTableWidget* eventTable_ = nullptr;
    QLabel* outputLabel_ = nullptr;
    QPushButton* openFolderButton_ = nullptr;
    // performance tab
    QLabel* perfTime_ = nullptr;
    QLabel* perfFps_ = nullptr;
    QLabel* perfCpu_ = nullptr;
    QLabel* perfMem_ = nullptr;
    QLabel* perfSpeedup_ = nullptr;
    QLabel* perfEfficiency_ = nullptr;
    QCheckBox* benchChecks_[4] = {nullptr, nullptr, nullptr, nullptr};
    QSpinBox* repeatsSpin_ = nullptr;
    QPushButton* benchButton_ = nullptr;
    QPushButton* benchStopButton_ = nullptr;
    QTableWidget* benchmarkTable_ = nullptr;
    QTextEdit* summaryText_ = nullptr;
    // graphs tab
    ChartWidget* charts_[4] = {nullptr, nullptr, nullptr, nullptr};
};
