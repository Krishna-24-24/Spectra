# Architecture

## 1. The big picture
```
┌────────────────────────── GUI thread (Qt) ──────────────────────────┐
│ MainWindow ── buttons/tables/graphs        VideoPlayer (own decoder) │
└──────────────┬───────────────────────────────────────▲──────────────┘
               │ startAnalysis / startBenchmark / stop │ Qt signals (queued)
        ┌──────▼───────────────────────────────────────┴──────┐
        │ ApplicationController: runs the engine on a worker   │
        │ std::thread, owns the atomic "cancel" flag           │
        └──────┬───────────────────────────────────────────────┘
               │  (plain C++, no Qt below this line)
        ┌──────▼──────────┐   ┌───────────────────┐
        │ VideoProcessor  │   │ BenchmarkManager  │ → runs VideoProcessor 1/2/4/8 threads
        └──────┬──────────┘   └───────────────────┘
   producer ───▼─── TaskQueue (bounded, mutex + 2 condition_variables)
                       │ pop
                 ThreadPool  (N workers, created once, reused)
                       │ FrameAnalyzer::analyze  (stateless → thread-safe)
                 ResultManager (mutex; re-orders by frame ID)
                       │ in order
                 EventDetector (streaming; groups frames into events)
```

## 2. Why each part exists
| Part | Job | Key idea |
|---|---|---|
| `TaskQueue` | hand frames from the producer to workers | **Producer-consumer.** Bounded size (so memory stays small): the producer *sleeps* when full, workers *sleep* when empty (`condition_variable`, no busy-waiting). `close()` = "no more work, finish what is queued"; `cancel()` = "drop everything and stop now". |
| `ThreadPool` | run tasks | **Threads are created once and reused**, not one thread per frame. Destructor cancels + joins, so nothing is left running. |
| `FrameAnalyzer` | measure one frame | shrink if wider than 1280 px → grayscale → Gaussian blur 9x9 → `absdiff` with previous frame → threshold 25 → `changedRatio`, largest contour (`largestRegionRatio`), 64-bin histogram distance (Bhattacharyya), brightness change. Holds no shared state, so many threads can call it at once. |
| `ResultManager` | fix the ordering | Workers finish in any order. Results wait in a `map<frameId, result>`; only the **next expected ID** is passed on. Same events for any thread count. |
| `EventDetector` | frames → events | See rules below. |
| `PerformanceMonitor` | measure | wall time (`steady_clock`), CPU % = process CPU time / (wall x logical cores) (`GetProcessTimes`), memory = sampled working set, peak kept. |
| `VideoProcessor` | one full run | Validates input, runs either the **sequential loop** (baseline) or **producer → queue → pool**. Progress is throttled; cancellation is checked by producer and workers. |
| `BenchmarkManager` | compare thread counts | Warm-up run (not recorded) + `repeats` timed runs per thread count, averaged. Speedup, efficiency and the text summary are **computed from the measured rows**. |
| `ReportWriter` | files | events CSV, benchmark CSV, processed MJPG `.avi` (red border + label while an event is active). |
| `ApplicationController` | Qt ↔ engine bridge | Engine on a `std::thread`; results return as signals, so the GUI thread is never blocked. Registered metatypes (`qRegisterMetaType`) let custom structs travel through signals. |
| `VideoPlayer` | playback | `QTimer` + its **own** `cv::VideoCapture`, independent of the analysis, so you can watch or seek while analysis runs. |
| `ChartWidget` | graphs | Drawn with `QPainter` (no Qt Charts dependency). |

## 3. Why every task carries the previous frame
Frame differencing needs frame *n* and frame *n-1*. If workers had to wait for each other to get the
previous frame, they could not run in parallel. Instead the producer attaches the previous frame to each
task (`cv::Mat` is reference-counted, so this is a pointer copy, not a pixel copy). Tasks are fully
independent.

## 4. Event rules (all thresholds live in `DetectorConfig`)
Each frame gets at most one label, checked in this order:

1. **Sudden Visual Change** – average brightness jumps by ≥ 40 levels (a flash).
2. **Scene Change** – ≥ 10 % of pixels changed **and** brightness histogram distance ≥ 0.4 (a hard cut).
3. **High Activity** – ≥ 10 % of pixels changed.
4. **Significant Motion** – ≥ 0.5 % of pixels changed.

Then:
* Labelled frames closer than **0.75 s** are merged into **one event** (otherwise 90 frames of one walk
  would be 90 events).
* Motion bursts shorter than **3 frames** are ignored as noise.
* A motion event is **High Activity** if at least half of its frames were intense.
* **Score** 0–1: motion = peak changed ratio ÷ 0.25 (capped at 1); scene change = changed ratio;
  sudden change = brightness jump ÷ 100.

## 4b. One hidden trap: OpenCV's own threads
OpenCV can split a single blur / resize / difference over several cores by itself. If that stays on,
a "1 thread" baseline is secretly parallel and N workers x OpenCV's threads fight for the CPU (we saw
8 threads become *slower* than 1 on a 12-processor laptop, and the 1-thread CPU usage was far above
1 core). `VideoProcessor::run` therefore calls `cv::setNumThreads(1)` for the duration of a run
(restored afterwards), so the only parallelism measured is our own ThreadPool.

## 5. Safe Stop
`stopWork()` sets `std::atomic<bool> cancel`. The producer sees it and stops reading; `TaskQueue::cancel()`
drops queued frames and wakes every sleeping thread; workers finish the frame they hold; `ThreadPool` joins.
Nothing is killed forcefully, and the GUI becomes usable again immediately after.

## 6. Error handling
Missing file, not a video, unreadable first frame, thread count outside 1–64, no detection type selected,
output folder not writable, video that stops decoding early (warning), unexpected exception inside a worker
(caught; the worker keeps running). Each gives a plain message, never a crash.

## 7. Testing
`ctest` runs 7 programs (see README). The most important: **identical events for 1/2/4/8 threads**,
**sample video vs ground truth**, **cancel never hangs**, **out-of-order results are re-ordered**.
