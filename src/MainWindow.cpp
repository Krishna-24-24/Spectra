#include "MainWindow.hpp"

#include "ChartWidget.hpp"
#include "ReportWriter.hpp"
#include "VideoPlayer.hpp"
#include "VideoView.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString mmss(double seconds)
{
    return QString::fromStdString(spectra::formatTimestamp(seconds, false));
}

QTableWidget* makeTable(const QStringList& headers)
{
    auto* t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->verticalHeader()->setVisible(false);
    t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    t->setAlternatingRowColors(true);
    return t;
}

void setCell(QTableWidget* t, int row, int col, const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    t->setItem(row, col, item);
}

QLabel* valueLabel(const QString& text = "-")
{
    auto* l = new QLabel(text);
    l->setStyleSheet("font-weight: bold;");
    return l;
}

} // namespace

// ===========================================================================
// construction
// ===========================================================================
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , controller_(new ApplicationController(this))
    , player_(new VideoPlayer(this))
{
    setWindowTitle("Multi-Threaded Video Analysis & Event Detection");
    resize(1500, 900);
    buildUi();

    connect(player_, &VideoPlayer::frameReady, videoView_, &VideoView::setFrame);
    connect(player_, &VideoPlayer::positionChanged, this, &MainWindow::onPlayerPosition);
    connect(player_, &VideoPlayer::playbackStateChanged, this, &MainWindow::onPlaybackState);

    connect(controller_, &ApplicationController::busyChanged, this, &MainWindow::onBusyChanged);
    connect(controller_, &ApplicationController::analysisProgress, this, &MainWindow::onProgress);
    connect(controller_, &ApplicationController::analysisFinished, this, &MainWindow::onAnalysisFinished);
    connect(controller_, &ApplicationController::benchmarkStep, this, &MainWindow::onBenchmarkStep);
    connect(controller_, &ApplicationController::benchmarkFinished, this, &MainWindow::onBenchmarkFinished);
    connect(controller_, &ApplicationController::failed, this, &MainWindow::onFailed);
    connect(controller_, &ApplicationController::statusMessage, this, [this](const QString& text) {
        statusLabel_->setText(text);
        statusBar()->showMessage(text);
    });

    updateButtons();
    statusBar()->showMessage("Ready. Click \"Browse Video\" to begin.");
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    controller_->stopWork();   // the controller's destructor then waits for the worker thread
    player_->pause();
    event->accept();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(buildLeftColumn());
    scroll->setMinimumWidth(560);
    scroll->setMaximumWidth(680);

    layout->addWidget(scroll, 0);
    layout->addWidget(buildRightColumn(), 1);
    setCentralWidget(central);

    setStyleSheet(
        "QGroupBox { font-weight: bold; border: 1px solid #c8c8c8; border-radius: 6px; margin-top: 10px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QPushButton#startButton { background: #2e9e4f; color: white; font-weight: bold; padding: 7px 14px; border-radius: 4px; }"
        "QPushButton#startButton:disabled { background: #a9c9b3; }"
        "QPushButton#stopAnalysisButton, QPushButton#benchStopButton { background: #c93a3a; color: white; font-weight: bold; padding: 7px 14px; border-radius: 4px; }"
        "QPushButton#stopAnalysisButton:disabled, QPushButton#benchStopButton:disabled { background: #d9aaaa; }"
        "QPushButton#benchButton { background: #2f6fbf; color: white; font-weight: bold; padding: 7px 14px; border-radius: 4px; }"
        "QPushButton#benchButton:disabled { background: #a6bddb; }");
}

QWidget* MainWindow::buildLeftColumn()
{
    auto* column = new QWidget;
    auto* v = new QVBoxLayout(column);
    v->setContentsMargins(0, 0, 6, 0);

    // ---- 1. Video input ---------------------------------------------------
    auto* inputBox = new QGroupBox("1. Video Input");
    auto* inputLayout = new QVBoxLayout(inputBox);
    browseButton_ = new QPushButton("Browse Video...");
    pathLabel_ = new QLabel("No video selected");
    pathLabel_->setWordWrap(true);
    pathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    metaLabel_ = new QLabel;
    metaLabel_->setTextFormat(Qt::RichText);
    inputLayout->addWidget(browseButton_);
    inputLayout->addWidget(pathLabel_);
    inputLayout->addWidget(metaLabel_);
    connect(browseButton_, &QPushButton::clicked, this, &MainWindow::onBrowse);
    v->addWidget(inputBox);

    // ---- 2. Player ----------------------------------------------------------
    auto* playerBox = new QGroupBox("2. Video Player");
    auto* playerLayout = new QVBoxLayout(playerBox);
    videoView_ = new VideoView;
    videoView_->setMinimumHeight(190);
    slider_ = new QSlider(Qt::Horizontal);
    slider_->setRange(0, 0);
    timeLabel_ = new QLabel("00:00:00 / 00:00:00");
    timeLabel_->setAlignment(Qt::AlignCenter);
    playButton_ = new QPushButton("Play");
    stopButton_ = new QPushButton("Stop");
    auto* playRow = new QHBoxLayout;
    playRow->addWidget(playButton_);
    playRow->addWidget(stopButton_);
    playRow->addWidget(timeLabel_, 1);
    playerLayout->addWidget(videoView_, 1);
    playerLayout->addWidget(slider_);
    playerLayout->addLayout(playRow);
    connect(playButton_, &QPushButton::clicked, this, &MainWindow::onPlayPause);
    connect(stopButton_, &QPushButton::clicked, player_, &VideoPlayer::stop);
    connect(slider_, &QSlider::valueChanged, this, &MainWindow::onSliderChanged);
    v->addWidget(playerBox, 1);

    // ---- 3. Settings ----------------------------------------------------------
    auto* settingsBox = new QGroupBox("3. Analysis Settings");
    auto* settingsLayout = new QGridLayout(settingsBox);
    singleRadio_ = new QRadioButton("Single-threaded");
    multiRadio_ = new QRadioButton("Multi-threaded");
    multiRadio_->setChecked(true);
    workersCombo_ = new QComboBox;
    for (int n : {1, 2, 4, 8}) {
        workersCombo_->addItem(QString::number(n) + (n == 1 ? " worker" : " workers"), n);
    }
    workersCombo_->setCurrentIndex(2);   // 4 workers
    motionCheck_ = new QCheckBox("Motion detection");
    sceneCheck_ = new QCheckBox("Scene change detection");
    activityCheck_ = new QCheckBox("Activity detection (high activity)");
    for (auto* c : {motionCheck_, sceneCheck_, activityCheck_}) {
        c->setChecked(true);
    }
    processedCheck_ = new QCheckBox("Also save a processed video (events marked in red)");
    processedCheck_->setChecked(true);
    startButton_ = new QPushButton("START ANALYSIS");
    startButton_->setObjectName("startButton");
    stopAnalysisButton_ = new QPushButton("STOP");
    stopAnalysisButton_->setObjectName("stopAnalysisButton");

    settingsLayout->addWidget(singleRadio_, 0, 0);
    settingsLayout->addWidget(multiRadio_, 0, 1);
    settingsLayout->addWidget(workersCombo_, 0, 2);
    settingsLayout->addWidget(motionCheck_, 1, 0, 1, 3);
    settingsLayout->addWidget(sceneCheck_, 2, 0, 1, 3);
    settingsLayout->addWidget(activityCheck_, 3, 0, 1, 3);
    settingsLayout->addWidget(processedCheck_, 4, 0, 1, 3);
    settingsLayout->addWidget(startButton_, 5, 0, 1, 2);
    settingsLayout->addWidget(stopAnalysisButton_, 5, 2);
    connect(singleRadio_, &QRadioButton::toggled, this, &MainWindow::onModeChanged);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startAnalysis);
    connect(stopAnalysisButton_, &QPushButton::clicked, controller_, &ApplicationController::stopWork);
    v->addWidget(settingsBox);

    // ---- 4. Live status ----------------------------------------------------------
    auto* statusBox = new QGroupBox("4. Live Status");
    auto* statusLayout = new QFormLayout(statusBox);
    progressBar_ = new QProgressBar;
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    framesLabel_ = valueLabel();
    fpsLabel_ = valueLabel();
    workersLabel_ = valueLabel();
    eventsLabel_ = valueLabel();
    statusLabel_ = new QLabel("Idle");
    statusLabel_->setWordWrap(true);
    statusLayout->addRow("Progress:", progressBar_);
    statusLayout->addRow("Frames processed:", framesLabel_);
    statusLayout->addRow("Processing speed:", fpsLabel_);
    statusLayout->addRow("Active workers:", workersLabel_);
    statusLayout->addRow("Events found:", eventsLabel_);
    statusLayout->addRow("Status:", statusLabel_);
    v->addWidget(statusBox);

    return column;
}

QWidget* MainWindow::buildRightColumn()
{
    tabs_ = new QTabWidget;
    tabs_->addTab(buildEventsTab(), "Detected Events");
    tabs_->addTab(buildPerformanceTab(), "Performance && Benchmark");
    tabs_->addTab(buildGraphsTab(), "Graphs");
    return tabs_;
}

QWidget* MainWindow::buildEventsTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    auto* hint = new QLabel("Click an event to jump to that moment in the video.");
    hint->setStyleSheet("color: #555;");
    eventTable_ = makeTable({"Timestamp", "Event Type", "Score", "Duration"});
    connect(eventTable_, &QTableWidget::cellClicked, this, &MainWindow::onEventClicked);

    outputLabel_ = new QLabel("Reports are saved automatically when an analysis finishes.");
    outputLabel_->setWordWrap(true);
    outputLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    openFolderButton_ = new QPushButton("Open output folder");
    connect(openFolderButton_, &QPushButton::clicked, this, &MainWindow::onOpenOutputFolder);

    v->addWidget(hint);
    v->addWidget(eventTable_, 1);
    v->addWidget(outputLabel_);
    v->addWidget(openFolderButton_, 0, Qt::AlignLeft);
    return page;
}

QWidget* MainWindow::buildPerformanceTab()
{
    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);

    // ---- last analysis -------------------------------------------------------------
    auto* perfBox = new QGroupBox("Performance of the last analysis");
    auto* grid = new QGridLayout(perfBox);
    perfTime_ = valueLabel();
    perfFps_ = valueLabel();
    perfCpu_ = valueLabel();
    perfMem_ = valueLabel();
    perfSpeedup_ = valueLabel();
    perfEfficiency_ = valueLabel();
    grid->addWidget(new QLabel("Processing time:"), 0, 0);
    grid->addWidget(perfTime_, 0, 1);
    grid->addWidget(new QLabel("Throughput (FPS):"), 0, 2);
    grid->addWidget(perfFps_, 0, 3);
    grid->addWidget(new QLabel("CPU usage:"), 1, 0);
    grid->addWidget(perfCpu_, 1, 1);
    grid->addWidget(new QLabel("Peak memory:"), 1, 2);
    grid->addWidget(perfMem_, 1, 3);
    grid->addWidget(new QLabel("Speedup:"), 2, 0);
    grid->addWidget(perfSpeedup_, 2, 1);
    grid->addWidget(new QLabel("Efficiency:"), 2, 2);
    grid->addWidget(perfEfficiency_, 2, 3);
    v->addWidget(perfBox);

    // ---- benchmark controls ------------------------------------------------------------
    auto* benchBox = new QGroupBox("Benchmark: the same video analysed with different thread counts");
    auto* bl = new QVBoxLayout(benchBox);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Thread counts:"));
    const int counts[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; ++i) {
        benchChecks_[i] = new QCheckBox(QString::number(counts[i]));
        benchChecks_[i]->setChecked(true);
        row->addWidget(benchChecks_[i]);
    }
    benchChecks_[0]->setEnabled(false);   // 1 thread is always the baseline
    benchChecks_[0]->setToolTip("1 thread is always measured: it is the baseline for speedup.");
    row->addSpacing(14);
    row->addWidget(new QLabel("Repeats:"));
    repeatsSpin_ = new QSpinBox;
    repeatsSpin_->setRange(1, 5);
    repeatsSpin_->setValue(2);
    repeatsSpin_->setToolTip("Every thread count is measured this many times and averaged.");
    row->addWidget(repeatsSpin_);
    row->addStretch(1);
    benchButton_ = new QPushButton("RUN BENCHMARK");
    benchButton_->setObjectName("benchButton");
    benchStopButton_ = new QPushButton("STOP");
    benchStopButton_->setObjectName("benchStopButton");
    row->addWidget(benchButton_);
    row->addWidget(benchStopButton_);
    connect(benchButton_, &QPushButton::clicked, this, &MainWindow::startBenchmark);
    connect(benchStopButton_, &QPushButton::clicked, controller_, &ApplicationController::stopWork);
    bl->addLayout(row);

    benchmarkTable_ = makeTable({"Threads", "Time (s)", "FPS", "Speedup", "Efficiency (%)", "CPU (%)", "Memory (MB)"});
    benchmarkTable_->setMinimumHeight(170);
    bl->addWidget(benchmarkTable_);
    v->addWidget(benchBox);

    // ---- interpretation ---------------------------------------------------------------------
    auto* sumBox = new QGroupBox("Performance summary (generated from the measured numbers)");
    auto* sl = new QVBoxLayout(sumBox);
    summaryText_ = new QTextEdit;
    summaryText_->setReadOnly(true);
    summaryText_->setPlaceholderText("Run the benchmark to get an explanation of the results.");
    sl->addWidget(summaryText_);
    v->addWidget(sumBox, 1);
    return page;
}

QWidget* MainWindow::buildGraphsTab()
{
    auto* page = new QWidget;
    auto* grid = new QGridLayout(page);
    for (int i = 0; i < 4; ++i) {
        charts_[i] = new ChartWidget;
    }
    charts_[0]->setTitle("Processing time vs threads");
    charts_[0]->setAxisTitles("Threads", "Time (seconds)");
    charts_[1]->setTitle("Throughput (FPS) vs threads");
    charts_[1]->setAxisTitles("Threads", "Frames per second");
    charts_[1]->setDecimals(0);
    charts_[2]->setTitle("Speedup vs threads");
    charts_[2]->setAxisTitles("Threads", "Speedup (x)");
    charts_[3]->setTitle("Efficiency vs threads");
    charts_[3]->setAxisTitles("Threads", "Efficiency (%)");
    charts_[3]->setDecimals(0);
    grid->addWidget(charts_[0], 0, 0);
    grid->addWidget(charts_[1], 0, 1);
    grid->addWidget(charts_[2], 1, 0);
    grid->addWidget(charts_[3], 1, 1);
    return page;
}

// ===========================================================================
// helpers
// ===========================================================================
void MainWindow::showError(const QString& message)
{
    statusLabel_->setText("Error: " + message);
    statusBar()->showMessage("Error: " + message);
    if (dialogsEnabled_) {
        QMessageBox::warning(this, "Problem", message);
    }
}

void MainWindow::setMetadata(const spectra::VideoMetadata& meta, qint64 fileBytes)
{
    metaLabel_->setText(
        QString("<b>%1</b><br>%2 x %3 &nbsp;|&nbsp; %4 fps &nbsp;|&nbsp; %5 frames &nbsp;|&nbsp; %6 &nbsp;|&nbsp; %7 MB")
            .arg(QString::fromStdString(meta.fileName).toHtmlEscaped())
            .arg(meta.width).arg(meta.height)
            .arg(meta.fps, 0, 'f', 2)
            .arg(meta.frameCount)
            .arg(mmss(meta.durationSec))
            .arg(fileBytes / (1024.0 * 1024.0), 0, 'f', 2));
}

void MainWindow::showTab(int index)
{
    if (tabs_) {
        tabs_->setCurrentIndex(index);
    }
}

void MainWindow::clearResults()
{
    eventTable_->setRowCount(0);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    for (auto* l : {framesLabel_, fpsLabel_, workersLabel_, eventsLabel_, perfTime_, perfFps_, perfCpu_,
                    perfMem_, perfSpeedup_, perfEfficiency_}) {
        l->setText("-");
    }
    outputLabel_->setText("Reports are saved automatically when an analysis finishes.");
}

spectra::DetectorConfig MainWindow::detectorFromUi() const
{
    spectra::DetectorConfig d;
    d.detectMotion = motionCheck_->isChecked();
    d.detectSceneChange = sceneCheck_->isChecked();
    d.detectActivity = activityCheck_->isChecked();
    return d;
}

void MainWindow::updateButtons()
{
    const bool idleWithVideo = videoLoaded_ && !busy_;
    browseButton_->setEnabled(!busy_);
    startButton_->setEnabled(idleWithVideo);
    stopAnalysisButton_->setEnabled(busy_);
    benchButton_->setEnabled(idleWithVideo);
    benchStopButton_->setEnabled(busy_);
    singleRadio_->setEnabled(!busy_);
    multiRadio_->setEnabled(!busy_);
    workersCombo_->setEnabled(!busy_ && multiRadio_->isChecked());
    for (auto* c : {motionCheck_, sceneCheck_, activityCheck_, processedCheck_}) {
        c->setEnabled(!busy_);
    }
    for (int i = 1; i < 4; ++i) {
        benchChecks_[i]->setEnabled(!busy_);
    }
    repeatsSpin_->setEnabled(!busy_);
    playButton_->setEnabled(videoLoaded_);
    stopButton_->setEnabled(videoLoaded_);
    slider_->setEnabled(videoLoaded_);
}

// ===========================================================================
// video input + player
// ===========================================================================
void MainWindow::onBrowse()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Choose a video", QString(),
        "Video files (*.mp4 *.avi *.mov *.mkv *.wmv *.m4v);;All files (*.*)");
    if (!path.isEmpty()) {
        openVideoFile(path);
    }
}

bool MainWindow::openVideoFile(const QString& path)
{
    spectra::VideoMetadata meta;
    QString error;
    if (!controller_->loadVideo(path, meta, error)) {
        showError(error);
        return false;
    }
    QString playerError;
    player_->pause();
    if (!player_->open(path, playerError)) {
        showError(playerError);
        return false;
    }

    videoLoaded_ = true;
    videoPath_ = path;
    meta_ = meta;
    pathLabel_->setText(path);
    setMetadata(meta, QFileInfo(path).size());
    clearResults();

    updatingSlider_ = true;
    slider_->setRange(0, static_cast<int>(player_->durationSec() * 1000.0));
    slider_->setValue(0);
    updatingSlider_ = false;
    onPlayerPosition(0.0);

    statusLabel_->setText("Video loaded. Choose your settings and press START ANALYSIS.");
    statusBar()->showMessage("Loaded " + QFileInfo(path).fileName());
    updateButtons();
    return true;
}

void MainWindow::onPlayPause()
{
    if (player_->isPlaying()) {
        player_->pause();
    } else {
        player_->play();
    }
}

void MainWindow::onPlaybackState(bool playing)
{
    playButton_->setText(playing ? "Pause" : "Play");
}

void MainWindow::onPlayerPosition(double seconds)
{
    updatingSlider_ = true;
    slider_->setValue(static_cast<int>(seconds * 1000.0));
    updatingSlider_ = false;
    timeLabel_->setText(mmss(seconds) + " / " + mmss(player_->durationSec()));
}

void MainWindow::onSliderChanged(int value)
{
    if (!updatingSlider_) {
        player_->seek(value / 1000.0);
    }
}

void MainWindow::onEventClicked(int row, int)
{
    if (row < 0 || !videoLoaded_) {
        return;
    }
    const QTableWidgetItem* item = eventTable_->item(row, 0);
    if (!item) {
        return;
    }
    player_->pause();
    player_->seek(item->data(Qt::UserRole).toDouble());
}

// ===========================================================================
// analysis
// ===========================================================================
void MainWindow::onModeChanged()
{
    updateButtons();
}

void MainWindow::startAnalysis()
{
    if (!videoLoaded_) {
        showError("Please choose a video first (Browse Video).");
        return;
    }
    const spectra::DetectorConfig det = detectorFromUi();
    if (!det.detectMotion && !det.detectSceneChange && !det.detectActivity) {
        showError("Please tick at least one thing to detect (motion, scene change or activity).");
        return;
    }
    player_->pause();

    spectra::AnalysisSettings s;
    s.videoPath = videoPath_.toLocal8Bit().toStdString();
    s.useThreadPool = multiRadio_->isChecked();
    s.workers = s.useThreadPool ? workersCombo_->currentData().toInt() : 1;
    s.detector = det;

    clearResults();
    progressBar_->setRange(0, 0);   // "busy" animation until the first progress report
    workersLabel_->setText(QString::number(s.workers));
    controller_->startAnalysis(s, processedCheck_->isChecked());
}

void MainWindow::onBusyChanged(bool busy)
{
    busy_ = busy;
    updateButtons();
}

void MainWindow::onProgress(const spectra::ProgressInfo& info)
{
    if (info.totalFrames > 0) {
        progressBar_->setRange(0, 1000);
        const double frac = std::min(1.0, static_cast<double>(info.framesProcessed) / static_cast<double>(info.totalFrames));
        progressBar_->setValue(static_cast<int>(frac * 1000));
        progressBar_->setFormat(QString("%1%").arg(frac * 100.0, 0, 'f', 1));
        framesLabel_->setText(QString("%1 / %2").arg(info.framesProcessed).arg(info.totalFrames));
    } else {
        framesLabel_->setText(QString::number(info.framesProcessed));
    }
    fpsLabel_->setText(QString("%1 FPS").arg(info.fps, 0, 'f', 1));
    workersLabel_->setText(QString::number(info.workers));
    eventsLabel_->setText(QString::number(info.eventsFound));
}

void MainWindow::updateEventTable(const std::vector<spectra::EventInfo>& events)
{
    eventTable_->setRowCount(0);
    eventTable_->setRowCount(static_cast<int>(events.size()));
    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        const auto& e = events[i];
        setCell(eventTable_, i, 0, QString::fromStdString(spectra::formatTimestamp(e.startSec, true)));
        eventTable_->item(i, 0)->setData(Qt::UserRole, e.startSec);
        setCell(eventTable_, i, 1, spectra::eventTypeName(e.type));
        setCell(eventTable_, i, 2, QString::number(e.score, 'f', 2));
        setCell(eventTable_, i, 3, QString("%1 s").arg(e.duration(), 0, 'f', 2));
    }
}

void MainWindow::updatePerformancePanel(const AnalysisOutcome& o)
{
    const auto& r = o.result;
    perfTime_->setText(QString("%1 s").arg(r.perf.wallSeconds, 0, 'f', 2));
    perfFps_->setText(QString("%1 FPS").arg(r.fps, 0, 'f', 1));
    perfCpu_->setText(QString("%1 %").arg(r.perf.cpuPercent, 0, 'f', 1));
    perfMem_->setText(QString("%1 MB").arg(r.perf.peakMemoryMB, 0, 'f', 1));
    if (o.baselineSec > 0.0 && r.perf.wallSeconds > 0.0) {
        const double speedup = o.baselineSec / r.perf.wallSeconds;
        perfSpeedup_->setText(QString("%1 x").arg(speedup, 0, 'f', 2));
        perfEfficiency_->setText(QString("%1 %").arg(speedup / std::max(1, r.workers) * 100.0, 0, 'f', 1));
    } else {
        perfSpeedup_->setText("run a single-thread analysis or the benchmark");
        perfEfficiency_->setText("-");
    }
}

void MainWindow::onAnalysisFinished(const AnalysisOutcome& o)
{
    const auto& r = o.result;
    progressBar_->setRange(0, 1000);
    if (!r.ok) {
        progressBar_->setValue(0);
        showError(QString::fromStdString(r.error));
        return;
    }

    updateEventTable(r.events);
    eventsLabel_->setText(QString::number(r.events.size()));
    framesLabel_->setText(r.meta.frameCount > 0
                              ? QString("%1 / %2").arg(r.framesProcessed).arg(r.meta.frameCount)
                              : QString::number(r.framesProcessed));
    fpsLabel_->setText(QString("%1 FPS").arg(r.fps, 0, 'f', 1));

    if (!r.cancelled) {
        updatePerformancePanel(o);
    }
    if (r.cancelled) {
        statusLabel_->setText(QString("Stopped by the user after %1 frames. The events found so far are shown; nothing was saved.")
                                  .arg(r.framesProcessed));
        statusBar()->showMessage("Analysis stopped.");
        return;
    }

    progressBar_->setValue(1000);
    progressBar_->setFormat("100.0%");

    QStringList saved;
    if (!o.eventsCsvPath.isEmpty()) saved << "Events CSV: " + o.eventsCsvPath;
    if (!o.processedVideoPath.isEmpty()) saved << "Processed video: " + o.processedVideoPath;
    outputLabel_->setText(saved.isEmpty() ? "Nothing was saved." : saved.join("\n"));

    QString status = QString("Finished: %1 events in %2 s (%3 FPS, %4 worker%5).")
                         .arg(r.events.size()).arg(r.perf.wallSeconds, 0, 'f', 2).arg(r.fps, 0, 'f', 1)
                         .arg(r.workers).arg(r.workers == 1 ? "" : "s");
    for (const auto& w : r.warnings) status += " Warning: " + QString::fromStdString(w);
    if (!o.note.isEmpty()) status += " Note: " + o.note;
    statusLabel_->setText(status);
    statusBar()->showMessage("Analysis finished.");
    showTab(0);
}

// ===========================================================================
// benchmark
// ===========================================================================
void MainWindow::startBenchmark()
{
    if (!videoLoaded_) {
        showError("Please choose a video first (Browse Video).");
        return;
    }
    const spectra::DetectorConfig det = detectorFromUi();
    if (!det.detectMotion && !det.detectSceneChange && !det.detectActivity) {
        showError("Please tick at least one thing to detect (motion, scene change or activity).");
        return;
    }
    spectra::BenchmarkOptions o;
    o.videoPath = videoPath_.toLocal8Bit().toStdString();
    o.threadCounts.clear();
    const int counts[4] = {1, 2, 4, 8};
    for (int i = 0; i < 4; ++i) {
        if (benchChecks_[i]->isChecked()) o.threadCounts.push_back(counts[i]);
    }
    if (o.threadCounts.size() < 2) {
        showError("Tick at least one thread count besides 1 (for example 2, 4 or 8).");
        return;
    }
    o.repeats = repeatsSpin_->value();
    o.detector = det;

    player_->pause();
    benchmarkTable_->setRowCount(0);
    summaryText_->clear();
    for (auto* c : charts_) c->clear();
    progressBar_->setRange(0, 1000);
    progressBar_->setValue(0);
    controller_->startBenchmark(o);
}

void MainWindow::onBenchmarkStep(int done, int total, const QString& label)
{
    statusLabel_->setText(QString("Benchmark: finished %1 of %2 runs (%3)").arg(done).arg(total).arg(label));
}

void MainWindow::updateBenchmarkViews(const std::vector<spectra::BenchmarkRow>& rows)
{
    benchmarkTable_->setRowCount(0);
    benchmarkTable_->setRowCount(static_cast<int>(rows.size()));
    QVector<ChartSeries> time, fps, speed, eff;
    ChartSeries sTime, sFps, sSpeed, sEff, idealSpeed, idealEff;
    sTime.name = "Measured";    sTime.color = QColor(30, 110, 200);
    sFps.name = "Measured";     sFps.color = QColor(40, 150, 80);
    sSpeed.name = "Measured";   sSpeed.color = QColor(230, 130, 20);
    sEff.name = "Measured";     sEff.color = QColor(140, 70, 180);
    idealSpeed.name = "Ideal (speedup = threads)"; idealSpeed.color = QColor(140, 140, 140);
    idealSpeed.dashed = true; idealSpeed.markers = false; idealSpeed.valueLabels = false;
    idealEff.name = "Ideal (100%)"; idealEff.color = QColor(140, 140, 140);
    idealEff.dashed = true; idealEff.markers = false; idealEff.valueLabels = false;

    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[i];
        setCell(benchmarkTable_, i, 0, QString::number(r.threads));
        setCell(benchmarkTable_, i, 1, QString::number(r.timeSec, 'f', 3));
        setCell(benchmarkTable_, i, 2, QString::number(r.fps, 'f', 1));
        setCell(benchmarkTable_, i, 3, QString("%1 x").arg(r.speedup, 0, 'f', 2));
        setCell(benchmarkTable_, i, 4, QString::number(r.efficiency, 'f', 1));
        setCell(benchmarkTable_, i, 5, QString::number(r.cpuPercent, 'f', 1));
        setCell(benchmarkTable_, i, 6, QString::number(r.memoryMB, 'f', 1));

        sTime.points << QPointF(r.threads, r.timeSec);
        sFps.points << QPointF(r.threads, r.fps);
        sSpeed.points << QPointF(r.threads, r.speedup);
        idealSpeed.points << QPointF(r.threads, r.threads);
        sEff.points << QPointF(r.threads, r.efficiency);
        idealEff.points << QPointF(r.threads, 100.0);
    }
    charts_[0]->setSeries({sTime});
    charts_[1]->setSeries({sFps});
    charts_[2]->setSeries({sSpeed, idealSpeed});
    charts_[3]->setSeries({sEff, idealEff});
}

void MainWindow::onBenchmarkFinished(const BenchmarkOutcome& o)
{
    const auto& r = o.result;
    progressBar_->setValue(r.ok && !r.cancelled ? 1000 : 0);
    if (!r.ok) {
        showError(QString::fromStdString(r.error));
        return;
    }
    if (r.cancelled) {
        statusLabel_->setText("Benchmark stopped by the user. No results were recorded.");
        statusBar()->showMessage("Benchmark stopped.");
        return;
    }
    updateBenchmarkViews(r.rows);

    QString html = "<ul>";
    for (const auto& line : r.interpretation) {
        html += "<li>" + QString::fromStdString(line).toHtmlEscaped() + "</li>";
    }
    html += "</ul>";
    summaryText_->setHtml(html);

    const QString done = o.csvPath.isEmpty() ? QString("Benchmark finished.")
                                             : "Benchmark finished. Results saved to " + o.csvPath;
    statusLabel_->setText(done);
    statusBar()->showMessage("Benchmark finished.");
    showTab(1);
    if (!o.csvPath.isEmpty()) {
        outputLabel_->setText("Benchmark CSV: " + o.csvPath);
    }
}

void MainWindow::onFailed(const QString& message)
{
    showError(message);
}

void MainWindow::onOpenOutputFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(controller_->outputDirectory()));
}
