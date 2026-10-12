"""aliveness: is a live person in front of the camera, or a spoof?

Milestone 1: camera capture and physical/virtual device check.
"""

from ._core import (
    Camera,
    CameraBusyError,
    CameraError,
    CameraNotFoundError,
    DeviceInfo,
    Frame,
    list_cameras,
)

__all__ = [
    "Camera",
    "CameraBusyError",
    "CameraError",
    "CameraNotFoundError",
    "DeviceInfo",
    "Frame",
    "list_cameras",
]
