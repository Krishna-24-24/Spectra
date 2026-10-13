// GUI smoke test: opens the REAL main window (off-screen), loads the sample video,
// runs an analysis, clicks an event, runs a small benchmark, and saves screenshots.
#include "ChartWidget.hpp"
#include "MainWindow.hpp"
#include "VideoPlayer.hpp"
#include "test_common.hpp"

#include <QApplication>
#include <QEventLoop>
#include <QTableWidget>
#include <QTimer>

#include <cstdlib>
#include <iostream>

// Waits (with the event loop running, like a real window) until `signal` fires or timeout.
template <typename Sender, typename Signal>
static bool waitFor(Sender* sender, Signal signal, int timeoutMs)
{
    QEventLoop loop;
    bool fired = false;
    QObject::connect(sender, signal, &loop, [&](auto&&...) { fired = true; loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return fired;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const char* sample = std::getenv("SPECTRA_SAMPLE_VIDEO");
    if (!sample) {
        std::cout << "SPECTRA_SAMPLE_VIDEO not set - skipping\n";
        return 0;
    }

    MainWindow w;
    w.setDialogsEnabled(false);
    w.resize(1500, 900);
    w.show();

    // --- wrong things first: errors must be polite, nothing may crash -------------------
    CHECK(!w.openVideoFile("definitely_not_here.mp4"));
    w.startAnalysis();     // no video loaded -> friendly error, no crash
    CHECK(!w.controller()->isBusy());

    // --- load the real video ---------------------------------------------------------
    CHECK(w.openVideoFile(sample));
    CHECK(w.player()->isOpen());
    CHECK_NEAR(w.player()->durationSec(), 30.0, 0.5);

    // --- player: seek works -----------------------------------------------------------
    w.player()->seek(12.0);
    CHECK_NEAR(w.player()->positionSec(), 12.0, 0.1);
    w.player()->seek(0.0);

    // --- run an analysis (4 workers is the default) -------------------------------------------
    w.startAnalysis();
    CHECK(w.controller()->isBusy());
    CHECK(waitFor(w.controller(), &ApplicationController::analysisFinished, 120000));
    CHECK(!w.controller()->isBusy());
    CHECK(w.eventTable()->rowCount() == 5);

    // --- clicking an event seeks the video ------------------------------------------------------
    if (w.eventTable()->rowCount() == 5) {
        w.eventTable()->setCurrentCell(2, 0);
        emit w.eventTable()->cellClicked(2, 0);        // 3rd event: High Activity at 16 s
        CHECK_NEAR(w.player()->positionSec(), 16.0, 0.2);
        CHECK(w.eventTable()->item(2, 1)->text() == "High Activity");
    }
    app.processEvents();
    w.grab().save("gui_after_analysis.png");

    // --- stop works and the GUI recovers --------------------------------------------------------------
    w.startAnalysis();
    QTimer::singleShot(300, [&] { w.controller()->stopWork(); });
    CHECK(waitFor(w.controller(), &ApplicationController::analysisFinished, 60000));
    CHECK(!w.controller()->isBusy());

    // --- a short benchmark -------------------------------------------------------------------------------
    w.startBenchmark();
    CHECK(w.controller()->isBusy());
    CHECK(waitFor(w.controller(), &ApplicationController::benchmarkFinished, 600000));
    CHECK(w.benchmarkTable()->rowCount() == 4);
    for (int i = 0; i < 4; ++i) CHECK(w.chart(i)->hasData());
    app.processEvents();
    w.showTab(1);
    app.processEvents();
    w.grab().save("gui_tab_performance.png");
    w.showTab(2);
    app.processEvents();
    w.grab().save("gui_tab_graphs.png");

    return finishTest("test_gui_smoke");
}
