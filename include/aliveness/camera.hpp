#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace aliveness {

// ---- Errors ----------------------------------------------------------------

class CameraError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// No camera at the requested index (or no cameras at all).
class CameraNotFoundError : public CameraError {
public:
    using CameraError::CameraError;
};

// The camera exists but cannot be used right now (another app holds it,
// privacy settings block access, device was preempted, ...).
class CameraBusyError : public CameraError {
public:
    using CameraError::CameraError;
};

// ---- Data types ------------------------------------------------------------

struct DeviceInfo {
    std::string name;    // human-readable name, e.g. "Integrated Camera"
    std::string path;    // OS device path (Windows: MF symbolic link)
    std::string bus;     // "usb", "pci", "swd", "root", ... ("" if unknown)
    std::string driver;  // driver service name, e.g. "usbvideo"
    bool is_physical = false;
    std::string reason;  // why is_physical was decided (see device_check.hpp)
};

struct CameraConfig {
    int width = 640;
    int height = 480;
    int fps = 30;
};

// One captured frame. Owns its pixels: RGB, uint8, row-major, tightly packed
// (row stride = width * 3). Never reused after it is handed out, so Python
// can keep a view into `data` for as long as it holds the Frame.
struct Frame {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> data;
    uint64_t seq = 0;        // 0, 1, 2, ... per Camera; set by Camera::read()
    double timestamp = 0.0;  // seconds, device clock (from the backend)
    double host_time = 0.0;  // seconds, host monotonic clock at arrival
};

// ---- Backend interface -----------------------------------------------------

// One implementation per platform (Media Foundation, later V4L2) plus a fake
// one for tests. A backend instance drives at most one open device.
class CameraBackend {
public:
    virtual ~CameraBackend() = default;

    virtual std::vector<DeviceInfo> list_devices() = 0;
    virtual void open(int index, const CameraConfig& config) = 0;
    // Blocks until the next frame is available. Only width/height/data/
    // timestamp need to be filled in; Camera sets seq and host_time.
    virtual Frame read() = 0;
    virtual void close() = 0;
    virtual DeviceInfo device_info() const = 0;
};

// name: "auto" (platform default) or "fake".
std::unique_ptr<CameraBackend> make_backend(const std::string& name);

std::vector<DeviceInfo> list_cameras(const std::string& backend = "auto");

// ---- Camera ----------------------------------------------------------------

// The public object: owns a backend, numbers frames, and serializes access so
// read() and close() from different threads cannot race.
class Camera {
public:
    Camera(int index, const CameraConfig& config, const std::string& backend = "auto");
    ~Camera();

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    std::shared_ptr<Frame> read();
    void close();
    bool is_open() const;
    DeviceInfo device_info() const;
    const CameraConfig& config() const { return config_; }

private:
    mutable std::mutex mutex_;
    std::unique_ptr<CameraBackend> backend_;
    CameraConfig config_;
    DeviceInfo info_;
    bool open_ = false;
    uint64_t next_seq_ = 0;
};

}  // namespace aliveness
