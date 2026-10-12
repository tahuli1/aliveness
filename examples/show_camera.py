"""Live camera view with measured FPS and device info.

    python examples/show_camera.py                 # camera 0, 640x480 @ 30
    python examples/show_camera.py --index 1 --width 1280 --height 720
    python examples/show_camera.py --backend fake  # no webcam needed

OpenCV is used only to display frames; capture is done by aliveness.
Press q or Esc to quit.
"""

import argparse
import sys
import time

import cv2

import aliveness


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--index", type=int, default=0)
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=480)
    p.add_argument("--fps", type=int, default=30)
    p.add_argument("--backend", default="auto", choices=["auto", "fake"])
    args = p.parse_args()

    print("Cameras:")
    for i, info in enumerate(aliveness.list_cameras(backend=args.backend)):
        print(f"  [{i}] {info.name}")
        print(f"      path:     {info.path}")
        print(f"      bus:      {info.bus or '?'}   driver: {info.driver or '?'}")
        print(f"      physical: {info.is_physical}  ({info.reason})")

    try:
        cam = aliveness.Camera(args.index, width=args.width, height=args.height,
                               fps=args.fps, backend=args.backend)
    except aliveness.CameraError as e:
        print(f"\nCannot open camera {args.index}: {e}", file=sys.stderr)
        return 1

    info = cam.device_info()
    print(f"\nOpened [{args.index}] {info.name}, physical={info.is_physical}")

    window = f"aliveness - {info.name}"
    frames, t0, fps = 0, time.perf_counter(), 0.0
    with cam:
        while True:
            frame = cam.read()
            frames += 1
            now = time.perf_counter()
            if now - t0 >= 1.0:
                fps = frames / (now - t0)
                frames, t0 = 0, now
                print(f"\rFPS: {fps:5.1f}  seq: {frame.seq}  t: {frame.timestamp:8.3f}s", end="", flush=True)

            bgr = cv2.cvtColor(frame.image, cv2.COLOR_RGB2BGR)
            label = f"{fps:.1f} fps  {'PHYSICAL' if info.is_physical else 'VIRTUAL'}"
            cv2.putText(bgr, label, (10, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
            cv2.imshow(window, bgr)
            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27) or cv2.getWindowProperty(window, cv2.WND_PROP_VISIBLE) < 1:
                break

    cv2.destroyAllWindows()
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
