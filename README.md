# aliveness

A C++ library with Python bindings that uses a laptop webcam to answer one
question: **is a live person in front of the camera, or a spoof?**

The camera layer, the C++/Python boundary, threading, signal processing and the
threat model are written from scratch. Only facial landmark detection will use
a library (dlib, from Milestone 2 on).

> **Status: Milestone 1.** Camera capture on Windows (Media Foundation) plus a
> physical/virtual device check. No liveness signals yet.

## Build

Requirements: Windows 10/11, Visual Studio 2022+ with the C++ workload, CMake
3.20+, Python 3.9+.

```powershell
python -m venv .venv
.venv\Scripts\activate
pip install ".[test,examples]"     # builds the C++ extension via scikit-build-core
pytest                             # runs without a camera (fake backend)
python -m mypy.stubtest aliveness._core --allowlist tests/stubtest_allowlist.txt  # type stubs match the C++ module
python examples/show_camera.py     # live view, FPS and device info (q to quit)
```

On Linux and macOS the package builds with only the fake backend (V4L2 is
planned), so the tests run there too.

## Usage

```python
import aliveness

for i, info in enumerate(aliveness.list_cameras()):
    print(i, info.name, info.bus, info.driver, info.is_physical, info.reason)

with aliveness.Camera(0, width=640, height=480, fps=30) as cam:
    frame = cam.read()            # blocks for the next frame; GIL released while waiting
    img = frame.image             # numpy (H, W, 3) uint8 RGB, no copy
    print(frame.seq, frame.timestamp, frame.host_time)
    print(cam.device_info().is_physical)
```

Errors are raised as `aliveness.CameraError`, or as one of its subclasses:
`CameraNotFoundError` (bad index, nothing connected, unplugged) and
`CameraBusyError` (another app has the camera, or privacy settings block it).
An unsupported resolution/FPS raises `CameraError`, and the message lists the
modes the camera supports.

For tests or machines without a webcam, pass `backend="fake"` to `Camera` or
`list_cameras`. You get a deterministic test pattern at the requested FPS.

## Architecture

```
python/aliveness/        public Python API (re-exports _core)
python/aliveness/_core.pyi  type stubs for the C++ module (checked in CI by stubtest)
bindings/module.cpp      pybind11: Camera, Frame (buffer protocol), DeviceInfo, errors
include/aliveness/       public C++ headers
src/camera.cpp           Camera: owns a backend, numbers frames, locks read/close
src/device_check.cpp     classify_device(): physical vs virtual, pure function
src/fake_backend.cpp     test-pattern backend
src/win/mf_backend.cpp   Media Foundation backend (enumerate, open, mode select, read)
src/win/device_query.cpp CfgMgr32 lookup: device path -> enumerator + driver service
```

**Camera backends.** `CameraBackend` is a small interface with
`list_devices`, `open`, `read`, `close` and `device_info`. The Windows backend
opens the device with a synchronous `IMFSourceReader`. It picks the native
mode that exactly matches the requested width/height/FPS, preferring NV12,
then YUY2, then MJPG. It then asks Media Foundation for RGB32 output and
converts BGRA to packed RGB itself. OpenCV is not used for capture.

**Frames and lifetime.** Each `read()` returns a new `Frame` that owns its
pixel buffer. `frame.image` is a NumPy view whose `.base` is the `Frame`, and
`np.asarray(frame)` uses the buffer protocol. Either way the pixels stay valid
for as long as Python holds a reference. Buffers are never reused, so a held
frame is never overwritten.

**Threading.** `read()` releases the GIL while it blocks. A mutex inside
`Camera` serializes `read()` and `close()`, so closing from another thread
waits for the in-flight read instead of freeing the device under it. A
dedicated capture thread with a drop-oldest queue comes in a later milestone.

**Device check.** For every camera, the backend collects the MF symbolic link
(e.g. `\\?\usb#vid_046d&pid_085c&mi_00#...`), the device node's enumerator
(`USB`, `PCI`, `SWD`, `ROOT`) and its driver service (`usbvideo`, ...).
`classify_device()` then turns these into `is_physical` plus a human-readable
`reason`. The rules and their limits are documented in
[include/aliveness/device_check.hpp](include/aliveness/device_check.hpp).
This check is a hint, not a security boundary.

## Threat model

*Stub. It will be expanded as each milestone lands.*

**Goal:** decide whether the frames come from a live human face in front of a
real camera.

| Attack | Description | Addressed by |
|---|---|---|
| Printed photo | Photo of the victim held up to the camera | M2 blink (EAR), M3 depth from motion, M4 pulse (rPPG) |
| Screen replay | Photo or video of the victim shown on a phone/tablet | M3 depth (flat screen), M4 pulse, M5 screen artifacts (moiré / refresh flicker) |
| Video injection | Pre-recorded or synthetic video fed in through a virtual camera driver (OBS Virtual Camera, v4l2loopback, ...) | **M1 device check** (partial); M6 challenge-response against pre-recorded video |
| Combined | Several of the above at once | M6 score fusion |

Milestones: **M1** capture and device check · **M2** landmarks and blink ·
**M3** depth from motion · **M4** pulse (rPPG) · **M5** screen artifacts
(optional) · **M6** score fusion and challenge-response.

**Out of scope / known gaps:**
- An attacker with admin or kernel access can fake any device metadata, or
  hook the process.
- Hardware injection, such as an HDMI capture card or a USB device that
  emulates a webcam, passes the device check.
- 3D masks and deepfakes generated in real time.
