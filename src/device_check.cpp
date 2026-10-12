#include "aliveness/device_check.hpp"

#include <algorithm>
#include <cctype>

namespace aliveness {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// "\\?\usb#vid_046d&pid_085c#..." -> "usb". Empty if the path has no prefix.
std::string bus_from_path(const std::string& path) {
    const std::string prefix = "\\\\?\\";
    if (path.compare(0, prefix.size(), prefix) != 0) return "";
    auto end = path.find('#', prefix.size());
    if (end == std::string::npos) return "";
    return lower(path.substr(prefix.size(), end - prefix.size()));
}

// Substrings of names / drivers of common virtual cameras (lowercase).
const char* const kVirtualMarkers[] = {
    "obs virtual", "obs-camera", "v4l2loopback", "virtual cam", "virtualcam",
    "manycam", "xsplit", "snap camera", "akvcam", "droidcam", "epoccam",
};

}  // namespace

DeviceClass classify_device(const std::string& name, const std::string& path,
                            const std::string& enumerator, const std::string& driver) {
    DeviceClass out;
    out.bus = enumerator.empty() ? bus_from_path(path) : lower(enumerator);

    const std::string lname = lower(name);
    const std::string ldriver = lower(driver);
    for (const char* marker : kVirtualMarkers) {
        if (contains(lname, marker) || contains(ldriver, marker)) {
            out.reason = std::string("name/driver matches known virtual camera '") + marker + "'";
            return out;
        }
    }

    if (out.bus == "swd" || out.bus == "root") {
        out.reason = "software-enumerated device (bus '" + out.bus + "')";
        return out;
    }

    if (out.bus == "usb") {
        const std::string lpath = lower(path);
        if (contains(lpath, "vid_") && contains(lpath, "pid_")) {
            out.is_physical = true;
            out.reason = "USB device with vendor/product id";
        } else {
            out.reason = "USB bus but no vendor/product id in path";
        }
        return out;
    }

    if (out.bus == "pci" || out.bus == "acpi" || out.bus == "display") {
        out.is_physical = true;
        out.reason = "hardware bus '" + out.bus + "'";
        return out;
    }

    out.reason = out.bus.empty() ? "bus unknown" : "unrecognized bus '" + out.bus + "'";
    return out;
}

}  // namespace aliveness
