#pragma once

#include <string>

namespace aliveness {

struct DeviceNodeProps {
    std::string enumerator;  // e.g. "USB", "PCI", "SWD"
    std::string driver;      // service name, e.g. "usbvideo"
};

// Look up the device node behind a device interface path (the MF symbolic
// link) and read its enumerator and driver service. Fields are left empty
// if a lookup fails; the caller falls back to parsing the path.
DeviceNodeProps query_device_node(const std::wstring& interface_path);

}  // namespace aliveness
