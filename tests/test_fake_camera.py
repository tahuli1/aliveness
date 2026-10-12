"""Camera API tests using the fake backend (no webcam needed)."""

import gc
import threading

import numpy as np
import pytest

import aliveness


def open_fake(**kwargs):
    kwargs.setdefault("width", 64)
    kwargs.setdefault("height", 48)
    kwargs.setdefault("fps", 200)  # fast, so tests don't wait
    return aliveness.Camera(0, backend="fake", **kwargs)


def test_list_cameras_fake():
    cams = aliveness.list_cameras(backend="fake")
    assert len(cams) == 1
    assert cams[0].name == "Fake Camera"
    assert cams[0].is_physical is False


def test_frame_shape_and_dtype():
    with open_fake() as cam:
        frame = cam.read()
    img = frame.image
    assert img.shape == (48, 64, 3)
    assert img.dtype == np.uint8
    assert img.flags["C_CONTIGUOUS"]


def test_pixels_match_pattern():
    # Fake pattern for frame n: R = (x + n) % 256, G = y % 256, B = n % 256
    with open_fake() as cam:
        cam.read()
        img = cam.read().image  # frame n = 1
    assert tuple(img[0, 0]) == (1, 0, 1)
    assert tuple(img[5, 10]) == (11, 5, 1)


def test_seq_and_timestamps_increase():
    with open_fake() as cam:
        frames = [cam.read() for _ in range(5)]
    assert [f.seq for f in frames] == [0, 1, 2, 3, 4]
    ts = [f.timestamp for f in frames]
    host = [f.host_time for f in frames]
    assert ts == sorted(ts) and len(set(ts)) == 5
    assert host == sorted(host)


def test_image_is_zero_copy_view():
    with open_fake() as cam:
        frame = cam.read()
    a = frame.image
    b = np.asarray(frame)  # buffer protocol
    assert a.base is frame
    a[0, 0, 0] = 77
    assert b[0, 0, 0] == 77  # same memory


def test_image_outlives_frame_and_camera():
    cam = open_fake()
    img = cam.read().image  # Frame object is only referenced by img.base
    expected = img.copy()
    cam.close()
    del cam
    gc.collect()
    assert np.array_equal(img, expected)


def test_held_frames_are_not_overwritten():
    with open_fake() as cam:
        first = cam.read().image
        snapshot = first.copy()
        for _ in range(10):
            cam.read()
    assert np.array_equal(first, snapshot)


def test_device_info_and_properties():
    with open_fake(width=32, height=16, fps=100) as cam:
        info = cam.device_info()
        assert info.bus == "fake"
        assert not info.is_physical
        assert (cam.width, cam.height, cam.fps) == (32, 16, 100)
        assert cam.is_open
    assert not cam.is_open


def test_read_after_close_raises():
    cam = open_fake()
    cam.close()
    cam.close()  # closing twice is fine
    with pytest.raises(aliveness.CameraError, match="closed"):
        cam.read()


def test_bad_index_raises_not_found():
    with pytest.raises(aliveness.CameraNotFoundError, match="index 3"):
        aliveness.Camera(3, backend="fake")


def test_not_found_is_a_camera_error():
    assert issubclass(aliveness.CameraNotFoundError, aliveness.CameraError)
    assert issubclass(aliveness.CameraBusyError, aliveness.CameraError)


def test_invalid_config_raises():
    with pytest.raises(aliveness.CameraError, match="positive"):
        aliveness.Camera(0, width=0, backend="fake")


def test_unknown_backend_raises():
    with pytest.raises(aliveness.CameraError, match="unknown backend"):
        aliveness.list_cameras(backend="nope")


def test_read_releases_gil():
    # A slow camera (5 fps) must not block other Python threads while it waits.
    ticks = 0
    stop = threading.Event()

    def spin():
        nonlocal ticks
        while not stop.is_set():
            ticks += 1

    with aliveness.Camera(0, width=8, height=8, fps=5, backend="fake") as cam:
        cam.read()  # frame 0 is immediate
        t = threading.Thread(target=spin)
        t.start()
        cam.read()  # waits ~200 ms in C++
        stop.set()
        t.join()
    assert ticks > 1000
