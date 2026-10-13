# Sample video: `sample_events.mp4`

A 30-second, 1280x720, 30 fps test video made by `tools/make_sample_video.py`.
Because **we** made it, we know exactly what happens and when. That lets us check the
detector against the truth instead of just trusting it.

| Time (s)  | What happens in the video                       | Expected event            |
|-----------|--------------------------------------------------|---------------------------|
| 5 - 8     | one white box slides across the picture          | Significant Motion        |
| 11        | hard cut to a different (checkerboard) scene      | Scene Change              |
| 16 - 19   | 22 small boxes jump around the whole picture      | High Activity             |
| 22        | one single white flash frame                      | Sudden Visual Change      |
| 25 - 28   | a yellow box slides back across the picture       | Significant Motion        |

Result of the real analysis (identical with 1, 2, 4 or 8 threads):

| Detected at  | Event                 | Duration |
|--------------|-----------------------|----------|
| 00:00:05.000 | Significant Motion    | 3.03 s   |
| 00:00:11.000 | Scene Change          | 0.03 s   |
| 00:00:16.000 | High Activity         | 3.03 s   |
| 00:00:22.000 | Sudden Visual Change  | 0.07 s   |
| 00:00:25.000 | Significant Motion    | 3.03 s   |

This comparison runs automatically in `tests/test_engine.cpp` (ctest target `engine`).
To re-create the video: `python tools/make_sample_video.py` (needs `opencv-python`, `numpy`, and `ffmpeg`).
