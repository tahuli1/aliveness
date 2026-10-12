#include "aliveness/camera.hpp"

#include <chrono>

#include "backends.hpp"

namespace aliveness {

std::unique_ptr<CameraBackend> make_backend(const std::string& name) {
    if (name == "fake") return make_fake_backend();
    if (name == "auto") {
#ifdef _WIN32
        return make_mf_backend();
#else
        throw CameraError(
            "no camera backend for this platform yet (Linux V4L2 is planned); "
            "use backend='fake' for testing");
#endif
    }
    throw CameraError("unknown backend '" + name + "' (expected 'auto' or 'fake')");
}

std::vector<DeviceInfo> list_cameras(const std::string& backend) {
    return make_backend(backend)->list_devices();
}

Camera::Camera(int index, const CameraConfig& config, const std::string& backend)
    : backend_(make_backend(backend)), config_(config) {
    if (config.width <= 0 || config.height <= 0 || config.fps <= 0) {
        throw CameraError("width, height and fps must be positive (got " +
                          std::to_string(config.width) + "x" + std::to_string(config.height) +
                          " @ " + std::to_string(config.fps) + " fps)");
    }
    backend_->open(index, config);
    info_ = backend_->device_info();
    open_ = true;
}

Camera::~Camera() {
    try {
        close();
    } catch (...) {
        // Destructors must not throw; nothing useful to do here.
    }
}

std::shared_ptr<Frame> Camera::read() {
    // The lock is held while the backend blocks, so close() from another
    // thread waits for the current read to finish instead of freeing the
    // device under it.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) throw CameraError("camera is closed");

    auto frame = std::make_shared<Frame>(backend_->read());
    frame->seq = next_seq_++;
    frame->host_time = std::chrono::duration<double>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    return frame;
}

void Camera::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) return;
    open_ = false;
    backend_->close();
}

bool Camera::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

DeviceInfo Camera::device_info() const {
    // Cached at open time; still available after close().
    std::lock_guard<std::mutex> lock(mutex_);
    return info_;
}

}  // namespace aliveness
