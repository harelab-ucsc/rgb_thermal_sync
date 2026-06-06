#!/usr/bin/env python3
"""
Convert saved Boson .npy raw frames to viewable 8-bit PNGs
"""

import argparse
import multiprocessing as mp
from pathlib import Path

import cv2
import numpy as np


INPUT_DIR  = Path.home() / 'dataset' / 'boson_capture' / 'raw'
OUTPUT_DIR = Path.home() / 'dataset' / 'boson_capture' / 'png'


def crop_telemetry(frame: np.ndarray) -> np.ndarray:
    """Drop the 2 telemetry rows present on 514-row frames"""
    if frame.ndim == 2 and frame.shape[0] == 514:
        return frame[:-2, :]
    return frame


def to_8bit(frame: np.ndarray) -> np.ndarray:
    """Percentile stretch 16-bit → 8-bit for viewing"""
    f = frame.astype(np.float32)
    lo = np.percentile(f, 2)
    hi = np.percentile(f, 98)
    if hi <= lo:
        return np.zeros_like(frame, dtype=np.uint8)
    f = np.clip(f, lo, hi)
    return ((f - lo) / (hi - lo) * 255.0).astype(np.uint8)


def convert_one(args):
    npy_path, out_dir = args
    try:
        frame = np.load(npy_path)
        frame = crop_telemetry(frame)
        png   = to_8bit(frame)
        out_path = out_dir / (npy_path.stem + ".png")
        cv2.imwrite(str(out_path), png)
        return True
    except Exception as e:
        print(f"  ERROR {npy_path.name}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input",   default=str(INPUT_DIR),  help="Directory of .npy files")
    parser.add_argument("--output",  default=str(OUTPUT_DIR), help="Directory for output PNGs")
    parser.add_argument("--workers", type=int, default=4,     help="Parallel worker processes")
    args = parser.parse_args()

    in_dir  = Path(args.input)
    out_dir = Path(args.output)
    out_dir.mkdir(parents=True, exist_ok=True)

    files = sorted(in_dir.glob("*.npy"))
    if not files:
        print(f"No .npy files found in {in_dir}")
        return

    print(f"Converting {len(files)} frames  →  {out_dir}")

    tasks = [(f, out_dir) for f in files]

    with mp.Pool(args.workers) as pool:
        results = []
        for i, ok in enumerate(pool.imap_unordered(convert_one, tasks), 1):
            results.append(ok)
            if i % 100 == 0 or i == len(files):
                print(f"  {i}/{len(files)}")

    done  = sum(results)
    fails = len(results) - done
    print(f"Done. {done} converted, {fails} failed.")


if __name__ == "__main__":
    main()
