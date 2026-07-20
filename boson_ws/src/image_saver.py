#!/usr/bin/env python3

import queue
import threading
import time
from pathlib import Path

import cv2
import numpy as np
from flirpy.camera.boson import Boson

EXT_SYNC_DISABLE = 0
EXT_SYNC_MASTER  = 1
EXT_SYNC_SLAVE   = 2

GAIN_MODE_NAMES = {0: "HIGH_GAIN", 1: "LOW_GAIN", 2: "AUTO_GAIN", 3: "DUAL_GAIN", 4: "MANUAL_GAIN"}

RAW_DIR = Path.home() / 'dataset' / 'boson_capture' / 'raw'
RAW_DIR.mkdir(parents=True, exist_ok=True)

WRITE_QUEUE_MAXSIZE = 180  # 3 s headroom at 60 Hz


def writer_worker(q: queue.Queue, stop_event: threading.Event):
    while not stop_event.is_set() or not q.empty():
        try:
            path, frame = q.get(timeout=0.5)
        except queue.Empty:
            continue
        np.save(path, frame)
        q.task_done()


def main():
    cam = Boson()

    write_q    = queue.Queue(maxsize=WRITE_QUEUE_MAXSIZE)
    stop_event = threading.Event()
    writer     = threading.Thread(target=writer_worker, args=(write_q, stop_event), daemon=True)
    writer.start()

    try:
        # Find the Boson video device index (e.g. 32 for /dev/video32)
        device_id = cam.find_video_device()
        if device_id is None:
            raise RuntimeError("Boson video device not found")

        cam.cap = cv2.VideoCapture(
            device_id, cv2.CAP_V4L2,
            [cv2.CAP_PROP_FPS,          60,
             cv2.CAP_PROP_FOURCC,        cv2.VideoWriter_fourcc(*'Y16 '),
             cv2.CAP_PROP_CONVERT_RGB,   0]
        )
        if not cam.cap.isOpened():
            raise RuntimeError(f"Failed to open /dev/video{device_id}")
        print(f"V4L2 negotiated FPS: {cam.cap.get(cv2.CAP_PROP_FPS)}")

        averager = cam.get_averager()
        print(f"Averager state: {averager} (0=off, 1=on)")
        if averager == 1:
            print("WARNING: smart averager is ON -- each frame is a temporal blend of "
                  "several sensor frames, not one true instantaneous sample. This also "
                  "weakens the frame-to-frame sync this rig depends on. Disabling it "
                  "requires disconnecting/reconnecting the camera after set_averager(0).")

        gain_mode = cam.get_gain_mode()
        print(f"Gain mode: {gain_mode} ({GAIN_MODE_NAMES.get(gain_mode, 'UNKNOWN')})")
        if gain_mode == 2:
            print("WARNING: gain mode is AUTO_GAIN -- the camera may switch gain tables "
                  "mid-session, changing the linear count-to-flux scale partway through "
                  "a capture. Set a fixed gain mode (set_gain_mode) if you need one "
                  "consistent radiometric scale across the whole session.")

        # SLAVE mode: Boson follows 60 Hz sync signal driven by STM32 PA8 on VPC3
        cam.set_external_sync_mode(EXT_SYNC_SLAVE)
        time.sleep(0.2)
        print(f"External sync mode: {cam.get_external_sync_mode()} (2 = slave)")

        print(f"Saving to: {RAW_DIR.resolve()}")
        print("Capturing — Ctrl+C to stop")

        frame_idx     = 0
        saved_count   = 0
        empty_count   = 0
        dropped_count = 0
        duplicate_count = 0
        last_frame_counter = None
        t0 = time.monotonic()

        while True:
            frame = cam.grab()

            if frame is None:
                empty_count += 1
                continue

            frame = np.asarray(frame)

            if frame.ndim == 1:
                n = frame.size
                if frame_idx == 0:
                    if n == 640 * 514:
                        print("Telemetry rows: present (640x514) -- FFC state/gain mode/frame "
                              "counter are recoverable per frame.")
                    elif n == 640 * 512:
                        print("WARNING: telemetry rows NOT present (640x512) -- per-frame FFC "
                              "state, gain mode, and frame counter cannot be recovered from "
                              "these files. Enable the telemetry line on the camera if you "
                              "need that provenance.")
                if   n == 640 * 512: frame = frame.reshape((512, 640))
                elif n == 640 * 514: frame = frame.reshape((514, 640))
                else: continue

            if frame.dtype != np.uint16:
                frame = frame.astype(np.uint16)

            try:
                frame_counter = cam.get_frame_count()
            except Exception:
                frame_counter = None

            # Skip duplicated frames if the camera's frame counter didn't advance
            if frame_counter is not None and last_frame_counter is not None:
                if frame_counter == last_frame_counter:
                    duplicate_count += 1
                    continue

            last_frame_counter = frame_counter

            ts = time.time_ns()
            path = RAW_DIR / f"boson_{frame_idx:010d}_{ts}.npy"

            try:
                write_q.put_nowait((path, frame))
                saved_count += 1
            except queue.Full:
                dropped_count += 1

            frame_idx += 1

            if frame_idx % 60 == 0:
                fps = frame_idx / (time.monotonic() - t0)
                print(
                    f"FPS: {fps:.1f} | grabbed {frame_idx} | saved {saved_count} | "
                    f"dropped {dropped_count} | duplicate {duplicate_count} | "
                    f"empty {empty_count} | queue {write_q.qsize()}"
                )

    except KeyboardInterrupt:
        print("\nStopped. Flushing writes...")

    finally:
        stop_event.set()
        writer.join()
        try:
            cam.release()
        except Exception:
            pass
        elapsed = time.monotonic() - t0
        print(
            f"Done. {saved_count} saved, {dropped_count} dropped, "
            f"{duplicate_count} duplicate. Avg FPS: {frame_idx / elapsed:.1f}"
        )


if __name__ == "__main__":
    main()
