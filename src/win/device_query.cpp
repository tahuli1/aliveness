// Kept in its own file because devpkey.h needs <initguid.h> to define the
// DEVPKEY_* constants, and that must not leak into the Media Foundation code.

#include "device_query.hpp"

#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <cfgmgr32.h>

namespace aliveness {
namespace {

std::string to_utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    std::string out(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::string devnode_string(DEVINST inst, const DEVPROPKEY& key) {
    wchar_t buf[512];
    ULONG size = sizeof(buf);
    DEVPROPTYPE type = 0;
    if (CM_Get_DevNode_PropertyW(inst, &key, &type, reinterpret_cast<PBYTE>(buf), &size, 0) !=
            CR_SUCCESS ||
        type != DEVPROP_TYPE_STRING) {
        return "";
    }
    return to_utf8(buf);
}

}  // namespace

DeviceNodeProps query_device_node(const std::wstring& interface_path) {
    DeviceNodeProps props;

    // Interface path -> device instance id, e.g. USB\VID_046D&PID_085C&MI_00\7&...
    wchar_t instance_id[512];
    ULONG size = sizeof(instance_id);
    DEVPROPTYPE type = 0;
    if (CM_Get_Device_Interface_PropertyW(interface_path.c_str(), &DEVPKEY_Device_InstanceId,
                                          &type, reinterpret_cast<PBYTE>(instance_id), &size,
                                          0) != CR_SUCCESS) {
        return props;
    }

    DEVINST inst = 0;
    if (CM_Locate_DevNodeW(&inst, instance_id, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) {
        return props;
    }

    props.enumerator = devnode_string(inst, DEVPKEY_Device_EnumeratorName);
    props.driver = devnode_string(inst, DEVPKEY_Device_Service);
    return props;
}

}  // namespace aliveness
