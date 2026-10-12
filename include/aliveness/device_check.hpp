#pragma once

#include <string>

namespace aliveness {

// Result of classifying a capture device as physical hardware or software.
struct DeviceClass {
    std::string bus;
    bool is_physical = false;
    std::string reason;
};

// Decide whether a camera looks like real hardware or a virtual/software
// camera. Pure function (no OS calls) so it can be unit-tested anywhere.
//
// Inputs, all collected by the platform backend:
//   name        friendly name ("OBS Virtual Camera")
//   path        device path. Windows: MF symbolic link, e.g.
//               \\?\usb#vid_046d&pid_085c&mi_00#...#{guid}\global
//   enumerator  bus driver that created the device node ("USB", "PCI",
//               "SWD", "ROOT"); empty if unknown -> taken from `path`
//   driver      function driver / service name ("usbvideo"); may be empty
//
// What is checked, in order:
//   1. Name or driver matches a known virtual camera (OBS, v4l2loopback,
//      ManyCam, ...)                                    -> virtual
//   2. Bus is a software enumerator (SWD, ROOT)         -> virtual
//      Windows 11 virtual cameras (MFCreateVirtualCamera) live under SWD.
//   3. Bus is USB and the path carries a VID/PID         -> physical
//   4. Bus is PCI / ACPI / DISPLAY (MIPI laptop cameras) -> physical
//   5. Anything else                                     -> virtual ("unknown")
//
// Known limitations (this is a hint, not a security boundary):
//   - Everything here is reported by drivers. A malicious kernel driver can
//     claim any bus, VID/PID or name.
//   - Hardware that forwards someone else's video (an HDMI capture card, a
//     USB "webcam" emulated by a microcontroller) is correctly "physical" but
//     still an injection path.
//   - Some laptops expose real MIPI cameras through a software device
//     (sensor groups / Windows Studio Effects); those show up as virtual.
//   - DirectShow-only virtual cameras (older OBS) are not visible to Media
//     Foundation at all, so they never reach this check.
DeviceClass classify_device(const std::string& name, const std::string& path,
                            const std::string& enumerator, const std::string& driver);

}  // namespace aliveness
