"""Live camera view with measured FPS and device info.

    python examples/show_camera.py                 # camera 0, 640x480 @ 30
    python examples/show_camera.py --index 1 --width 1280 --height 720
    python examples/show_camera.py --backend fake  # no webcam needed

OpenCV is used only to display frames; capture is done by aliveness.
Press q or Esc to quit.
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Literal

import cv2
import numpy as np

import aliveness

QUIT_KEYS = (ord("q"), 27)  # q, Esc


class FpsMeter:
    """Frames per second, averaged over roughly one-second windows."""

    def __init__(self) -> None:
        """Start measuring from now."""
        self.fps: float = 0.0
        self._frames: int = 0
        self._start: float = time.perf_counter()

    def tick(self) -> bool:
        """Count one frame. Returns True when self.fps was just updated."""
        self._frames += 1
        elapsed = time.perf_counter() - self._start
        if elapsed < 1.0:
            return False
        self.fps = self._frames / elapsed
        self._frames = 0
        self._start = time.perf_counter()
        return True


def parse_args() -> argparse.Namespace:
    """Parse command line options: camera index, resolution, FPS and backend."""
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--index", type=int, default=0)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--backend", default="auto", choices=["auto", "fake"])
    return p.parse_args()


def print_cameras(backend: Literal["auto", "fake"]) -> None:
    """Print every camera the backend sees, with its device check result."""
    print("Cameras:")
    for i, info in enumerate(aliveness.list_cameras(backend=backend)):
        print(f"  [{i}] {info.name}")
        print(f"      path:     {info.path}")
        print(f"      bus:      {info.bus or '?'}   driver: {info.driver or '?'}")
        print(f"      physical: {info.is_physical}  ({info.reason})")


def open_camera(args: argparse.Namespace) -> aliveness.Camera | None:
    """Open the camera from the command line options, or return None on error."""
    try:
        return aliveness.Camera(args.index, width=args.width, height=args.height,
                                fps=args.fps, backend=args.backend)
    except aliveness.CameraError as e:
        print(f"\nCannot open camera {args.index}: {e}", file=sys.stderr)
        return None


def draw_overlay(frame: aliveness.Frame, fps: float, is_physical: bool) -> np.ndarray:
    """Return a BGR copy of the frame (for OpenCV) with FPS and device type drawn on it."""
    bgr = cv2.cvtColor(frame.image, cv2.COLOR_RGB2BGR)
    label = f"{fps:.1f} fps  {'PHYSICAL' if is_physical else 'VIRTUAL'}"
    cv2.putText(bgr, label, (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
    return bgr


def should_quit(window: str) -> bool:
    """Return True if the user pressed q / Esc or closed the window.

    Also gives OpenCV its 1 ms to process window events; imshow needs this.
    """
    key = cv2.waitKey(1) & 0xFF
    return key in QUIT_KEYS or cv2.getWindowProperty(window, cv2.WND_PROP_VISIBLE) < 1


def show_live(cam: aliveness.Camera) -> None:
    """Show frames until the user quits, printing FPS once per second."""
    info = cam.device_info()
    window = f"aliveness - {info.name}"
    meter = FpsMeter()
    while True:
        frame = cam.read()
        if meter.tick():
            print(f"\rFPS: {meter.fps:5.1f}  seq: {frame.seq}  t: {frame.timestamp:8.3f}s",
                  end="", flush=True)
        cv2.imshow(window, draw_overlay(frame, meter.fps, info.is_physical))
        if should_quit(window):
            break
    cv2.destroyAllWindows()
    print()


def main() -> int:
    """List cameras, open the chosen one and show it. Returns the exit code."""
    args = parse_args()
    print_cameras(args.backend)

    cam = open_camera(args)
    if cam is None:
        return 1

    info = cam.device_info()
    print(f"\nOpened [{args.index}] {info.name}, physical={info.is_physical}")
    with cam:
        show_live(cam)
    return 0


if __name__ == "__main__":
    sys.exit(main())
