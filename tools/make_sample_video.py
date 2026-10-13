"""
Creates data/sample_videos/sample_events.mp4 - a 30 second test video with events
whose positions are KNOWN, so the detector can be checked against a ground truth.

    python tools/make_sample_video.py            (needs: pip install opencv-python numpy,
                                                  and ffmpeg on the PATH for the H.264 encode)

Ground truth (seconds) - also listed in data/sample_videos/GROUND_TRUTH.md:

     5.0 -  8.0  a small box moves across the picture          -> Significant Motion
    11.0         hard cut to a different background             -> Scene Change
    16.0 - 19.0  many boxes moving fast all over the picture    -> High Activity
    22.0         one-frame-long white flash                     -> Sudden Visual Change
    25.0 - 28.0  the small box moves again                      -> Significant Motion
"""
import subprocess
import sys

import cv2
import numpy as np

W, H, FPS, SECONDS = 1280, 720, 30, 30
OUT = sys.argv[1] if len(sys.argv) > 1 else "data/sample_videos/sample_events.mp4"


def background(kind):
    yy, xx = np.mgrid[0:H, 0:W]
    if kind == 0:   # bluish gradient
        b = (80 + 100 * xx / W).astype(np.uint8)
        g = (70 + 60 * yy / H).astype(np.uint8)
        r = np.full((H, W), 60, np.uint8)
    else:           # greenish diagonal pattern, similar overall brightness
        pat = ((xx // 80 + yy // 80) % 2) * 40
        b = np.full((H, W), 50, np.uint8)
        g = (110 + pat).astype(np.uint8)
        r = (70 + 30 * yy / H).astype(np.uint8)
    return np.dstack([b, g, r]).astype(np.uint8)


bg = [background(0), background(1)]
rng = np.random.default_rng(7)
# 22 small boxes that jump 60-90 pixels per frame in random directions (very busy picture)
boxes = [(int(rng.integers(0, W - 90)), int(rng.integers(0, H - 90)),
          int(rng.choice([-1, 1]) * rng.integers(60, 90)), int(rng.choice([-1, 1]) * rng.integers(40, 70)))
         for _ in range(22)]

cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "bgr24", "-s", f"{W}x{H}",
       "-r", str(FPS), "-i", "-", "-an", "-c:v", "libx264", "-preset", "veryfast", "-crf", "23",
       "-pix_fmt", "yuv420p", OUT]
ff = subprocess.Popen(cmd, stdin=subprocess.PIPE)

for i in range(FPS * SECONDS):
    t = i / FPS
    frame = bg[1 if 11.0 <= t < 30 else 0].copy()

    if 5.0 <= t < 8.0:                                   # small moving box
        x = int(60 + (t - 5.0) / 3.0 * 1000)
        cv2.rectangle(frame, (x, 250), (x + 200, 450), (245, 245, 245), -1)
    if 25.0 <= t < 28.0:
        x = int(1000 - (t - 25.0) / 3.0 * 900)
        cv2.rectangle(frame, (x, 270), (x + 200, 470), (30, 200, 230), -1)
    if 16.0 <= t < 19.0:                                 # lots of moving boxes
        for k, (bx, by, vx, vy) in enumerate(boxes):
            x = int((bx + vx * (i - 16 * FPS)) % (W - 90))
            y = int((by + vy * (i - 16 * FPS)) % (H - 90))
            cv2.rectangle(frame, (x, y), (x + 80, y + 80), (40 + 9 * k, 230 - 8 * k, 40 + 6 * k), -1)
    if i == 22 * FPS:                                    # one white flash frame
        frame[:] = 255

    ff.stdin.write(frame.tobytes())

ff.stdin.close()
ff.wait()
print("wrote", OUT)
