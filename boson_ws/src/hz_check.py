from flirpy.camera.boson import Boson
import time
import numpy as np

N = 300  # number of frames to measure

with Boson() as camera:
    # Warm up / discard first few frames
    for _ in range(10):
        camera.grab()

    timestamps = []

    for i in range(N):
        img = camera.grab()
        timestamps.append(time.perf_counter())

    timestamps = np.array(timestamps)
    dt = np.diff(timestamps)

    avg_fps = 1.0 / np.mean(dt)
    median_fps = 1.0 / np.median(dt)
    min_fps = 1.0 / np.max(dt)
    max_fps = 1.0 / np.min(dt)

    print(f"Frames captured: {N}")
    print(f"Average FPS: {avg_fps:.2f}")
    print(f"Median FPS:  {median_fps:.2f}")
    print(f"Min FPS:     {min_fps:.2f}")
    print(f"Max FPS:     {max_fps:.2f}")
    print(f"Mean frame interval: {np.mean(dt)*1000:.2f} ms")