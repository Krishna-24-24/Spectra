# Demo script (about 5 minutes)

**Before you start:** build in Release, close other heavy programs (they disturb timing).

1. **Intro (20 s)** – "It analyses a video in parallel, detects events with classic image processing (no AI),
   and measures how much faster multi-threading is."
2. **Browse Video** → `sample_events.mp4`. Point at the metadata. Press **Play**, drag the slider.
3. **Single-threaded analysis.** Mode = Single-threaded → **START ANALYSIS**. Show the live progress and FPS.
   Note the time in the Performance panel (this is the baseline).
4. **Multi-threaded analysis.** Mode = Multi-threaded, 4 workers → START. Show "Active workers: 4", faster FPS.
   Performance panel now shows **Speedup / Efficiency** versus your single-thread run.
5. **Events table.** "Same five events as with one thread - the ResultManager puts results back in frame
   order." **Click** "High Activity" → the video jumps to 00:00:16.
6. **Stop.** Start again, press **STOP** after a second: "threads finish cleanly, the window never froze."
7. **Benchmark** tab → RUN BENCHMARK (1/2/4/8). Show the table, then the **Graphs** tab, then read the
   generated summary aloud. Explain: speedup flattens near your number of CPU cores (Amdahl's law:
   decoding frames is not parallel).
8. **Open output folder** → show `events.csv`, `benchmark.csv`, `processed.avi` (red frame during events).

## Questions you will probably get
* *Why a thread pool and not a thread per frame?* Creating a thread costs time; reusing N workers avoids it.
* *Why a bounded queue?* So memory cannot grow without limit if the producer is faster than the workers.
* *Why can results arrive out of order and how do you fix it?* Frames take different times; ResultManager
  stores them by frame ID and releases only the next expected one.
* *Why is 8 threads not 8x faster?* Cores are limited, decoding is sequential (Amdahl), and threads cost
  coordination time. The benchmark shows the real numbers.
* *Is it AI?* No: grayscale, Gaussian blur, frame difference, threshold, contours, histogram.
* *How do you know detection is right?* A test video with known events (`GROUND_TRUTH.md`) is checked
  automatically.
