#!/usr/bin/env python3
"""Read a bounded set of RGB frames through the V1.1 FPC2 adapter."""
import argparse
import json
from pathlib import Path
import time


def capture(output):
    from maix import camera, image

    cam = None
    frame = None
    print("CAPTURE_BEGIN 640x480 RGB888 requested_fps=30 bus=7", flush=True)
    try:
        cam = camera.Camera(640, 480, image.Format.FMT_RGB888, fps=30)
        timestamps = []
        sampled_frames = set()
        nonuniform = 0
        for index in range(105):
            frame = cam.read(block_ms=2000)
            observed = time.monotonic()
            if frame is None:
                raise RuntimeError(f"Empty image at frame {index}")
            actual = (frame.width(), frame.height(), frame.format(), frame.data_size())
            expected = (640, 480, image.Format.FMT_RGB888, 640 * 480 * 3)
            if actual != expected:
                raise RuntimeError(f"Invalid image at frame {index}: {frame}")
            if index == 0:
                print("FIRST_FRAME width=640 height=480 format=RGB888", flush=True)
            if index >= 15:
                timestamps.append(observed)
                pixels = tuple(tuple(frame.get_pixel(x, y, True))
                               for y in range(30, 480, 60)
                               for x in range(40, 640, 80))
                if any(len(pixel) != 3 for pixel in pixels):
                    raise RuntimeError("Invalid RGB samples")
                sampled_frames.add(pixels)
                nonuniform += len(set(pixels)) > 1
            if index == 104:
                frame.save(str(output / "frame.png"))
            frame = None
        result = {
            "width": 640, "height": 480, "format": "RGB888",
            "requested_fps": 30, "warmup_frames": 15,
            "valid_frames": len(timestamps),
            "measured_fps": round(89 / (timestamps[-1] - timestamps[0]), 2),
            "distinct_sampled_frames": len(sampled_frames),
            "nonuniform_sampled_frames": nonuniform,
        }
        (output / "capture.json").write_text(json.dumps(result, indent=2) + "\n")
        print("CAPTURE_RESULT " + json.dumps(result), flush=True)
    finally:
        frame = None
        if cam is not None:
            cam.close()
        print("CAMERA_CLOSED", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    capture(parser.parse_args().out)
