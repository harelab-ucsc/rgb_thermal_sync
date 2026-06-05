#!/usr/bin/env python3

import time
from pathlib import Path

import cv2
import numpy as np
import tifffile as tiff
from flirpy.camera.boson import Boson

# flirpy Boson sync enums
EXT_SYNC_DISABLE = 0
EXT_SYNC_MASTER = 1
EXT_SYNC_SLAVE = 2

# Output folders
BASE_DIR = Path("boson_capture")
RAW_DIR = BASE_DIR / "tiff_raw"
PNG_DIR = BASE_DIR / "png_preview"

RAW_DIR.mkdir(parents=True, exist_ok=True)
PNG_DIR.mkdir(parents=True, exist_ok=True)


def crop_telemetry_rows(frame: np.ndarray) -> np.ndarray:
    """
    If height is 514, drop the top 2 rows.
    """
    if frame is None:
        return frame

    if frame.ndim != 2:
        raise ValueError(f"Expected 2D grayscale frame, got shape {frame.shape}")

    h, w = frame.shape
    if h == 514:
        return frame[:-2, :]
    return frame


def make_viewable_png(frame_u16: np.ndarray) -> np.ndarray:
    """
    Convert a 16-bit Boson raw frame into an 8-bit viewable image using percentile stretch.
    """
    f = frame_u16.astype(np.float32)

    lo = np.percentile(f, 2)
    hi = np.percentile(f, 98)

    if hi <= lo:
        return np.zeros_like(frame_u16, dtype=np.uint8)

    f = np.clip(f, lo, hi)
    view = ((f - lo) / (hi - lo) * 255.0).astype(np.uint8)
    return view


def main():
    cam = Boson()

    try:
        print("Connected to Boson")
        try:
            print("Camera serial:", cam.get_camera_serial())
        except Exception:
            pass

        try:
            print("Part number:", cam.get_part_number())
        except Exception:
            pass

        try:
            print("Firmware:", cam.get_firmware_revision())
        except Exception:
            pass

        # Put Boson into external sync slave mode
        cam.set_external_sync_mode(EXT_SYNC_SLAVE)
        time.sleep(0.2)

        mode = cam.get_external_sync_mode()
        print("External sync mode:", mode)
        if mode != EXT_SYNC_SLAVE:
            raise RuntimeError(f"Failed to enter slave mode. Current mode = {mode}")

        # Open USB video stream as raw 16-bit Y16
        cam.setup_video()
        print("Video capture opened.")
        print(f"Saving TIFFs to: {RAW_DIR.resolve()}")
        print(f"Saving PNGs  to: {PNG_DIR.resolve()}")
        print("Waiting for frames from STM32 60 Hz sync pulses...")

        last_frame_counter = None
        saved_count = 0
        duplicate_count = 0
        empty_count = 0

        while True:
            frame = cam.grab()

            if frame is None:
                empty_count += 1
                continue

            frame = np.asarray(frame)

            # Some platforms return flattened arrays for Y16; repair if possible
            if frame.ndim == 1:
                if frame.size == 640 * 512:
                    frame = frame.reshape((512, 640))
                elif frame.size == 640 * 514:
                    frame = frame.reshape((514, 640))
                else:
                    print(f"Skipping unexpected flat frame shape: {frame.shape}")
                    continue

            frame = crop_telemetry_rows(frame)

            if frame.dtype != np.uint16:
                # Keep raw as uint16 for TIFF output
                frame = frame.astype(np.uint16)

            try:
                frame_counter = cam.get_frame_count()
            except Exception:
                frame_counter = None

            # Skip duplicated frames if frame counter didn't advance
            if frame_counter is not None and last_frame_counter is not None:
                if frame_counter == last_frame_counter:
                    duplicate_count += 1
                    continue

            last_frame_counter = frame_counter

            timestamp_ns = time.time_ns()

            if frame_counter is None:
                stem = f"boson_{timestamp_ns}"
            else:
                stem = f"boson_fc{frame_counter:010d}_{timestamp_ns}"

            raw_path = RAW_DIR / f"{stem}.tiff"
            png_path = PNG_DIR / f"{stem}.png"

            # Save raw scientific image
            tiff.imwrite(raw_path, frame)

            # Save viewable preview
            preview = make_viewable_png(frame)
            cv2.imwrite(str(png_path), preview)

            saved_count += 1

            if saved_count % 30 == 0:
                print(
                    f"Saved {saved_count} frames | "
                    f"duplicates skipped: {duplicate_count} | "
                    f"empty grabs: {empty_count} | "
                    f"latest: {raw_path.name}"
                )

    except KeyboardInterrupt:
        print("\nStopped by user.")

    finally:
        try:
            cam.release()
        except Exception:
            pass

        try:
            cam.set_external_sync_mode(EXT_SYNC_DISABLE)
        except Exception:
            pass

        print("Camera released. External sync disabled.")


if __name__ == "__main__":
    main()