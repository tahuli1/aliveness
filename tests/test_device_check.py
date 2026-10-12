"""Physical/virtual classification rules (pure logic, no camera needed)."""

import pytest

from aliveness._core import _classify_device as classify

USB_LINK = r"\\?\usb#vid_046d&pid_085c&mi_00#7&1a2b3c4d&0&0000#{e5323777-f976-4f5b-9b55-b94699c46e44}\global"
SWD_LINK = r"\\?\swd#vcamdevapi#{a1b2c3d4-0000-0000-0000-000000000000}#{e5323777-f976-4f5b-9b55-b94699c46e44}\global"


@pytest.mark.parametrize(
    "name, path, enumerator, driver, bus, physical",
    [
        # Typical USB webcam (external or built into a laptop)
        ("Logitech C922", USB_LINK, "USB", "usbvideo", "usb", True),
        # Enumerator unknown -> bus taken from the path
        ("Logitech C922", USB_LINK, "", "", "usb", True),
        # Windows 11 virtual camera (MFCreateVirtualCamera) lives under SWD
        ("Some Camera", SWD_LINK, "SWD", "", "swd", False),
        # Software device under ROOT
        ("Some Camera", r"\\?\root#image#0000#{guid}\global", "ROOT", "", "root", False),
        # Known virtual camera by name, even on a hardware-looking bus
        ("OBS Virtual Camera", USB_LINK, "USB", "usbvideo", "usb", False),
        # Linux v4l2loopback by driver name
        ("Dummy video device", "/dev/video9", "platform", "v4l2loopback", "platform", False),
        # MIPI laptop camera on PCI
        ("Integrated Camera", r"\\?\pci#ven_8086&dev_a75d#3&11583659&0&28#{guid}\global", "PCI", "", "pci", True),
        # USB without VID/PID -> not trusted
        ("Odd Camera", r"\\?\usb#something#{guid}", "USB", "", "usb", False),
        # Nothing known
        ("Mystery", "", "", "", "", False),
    ],
)
def test_classify(name, path, enumerator, driver, bus, physical):
    got_bus, got_physical, reason = classify(name, path, enumerator, driver)
    assert got_bus == bus
    assert got_physical is physical
    assert reason  # always explains the decision
