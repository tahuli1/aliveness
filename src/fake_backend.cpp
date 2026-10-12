// Fake backend: one device that produces a deterministic test pattern at the
// requested frame rate. Used by the tests and for running without a webcam.
//
// Pattern for frame n at pixel (x, y):  R = (x + n) % 256, G = y % 256, B = n % 256

#include <chrono>
#include <thread>

#include "backends.hpp"

namespace aliveness {
namespace {

using Clock = std::chrono::steady_clock;

DeviceInfo fake_device_info() {
    DeviceInfo info;
    info.name = "Fake Camera";
    info.path = "fake://0";
    info.bus = "fake";
    info.driver = "aliveness-fake";
    info.is_physical = false;
    info.reason = "fake test backend";
    return info;
}

class FakeBackend : public CameraBackend {
public:
    std::vector<DeviceInfo> list_devices() override { return {fake_device_info()}; }

    void open(int index, const CameraConfig& config) override {
        if (index != 0) {
            throw CameraNotFoundError("camera index " + std::to_string(index) +
                                      " not found (fake backend has 1 camera, index 0)");
        }
        config_ = config;
        frame_count_ = 0;
        start_ = Clock::now();
    }

    Frame read() override {
        // Pace frames like a real camera: frame n is due at start + n / fps.
        auto due = start_ + std::chrono::duration_cast<Clock::duration>(
                                std::chrono::duration<double>(double(frame_count_) / config_.fps));
        std::this_thread::sleep_until(due);

        Frame f;
        f.width = config_.width;
        f.height = config_.height;
        f.data.resize(size_t(f.width) * f.height * 3);
        const uint64_t n = frame_count_;
        uint8_t* p = f.data.data();
        for (int y = 0; y < f.height; ++y) {
            for (int x = 0; x < f.width; ++x) {
                *p++ = uint8_t((x + n) % 256);
                *p++ = uint8_t(y % 256);
                *p++ = uint8_t(n % 256);
            }
        }
        f.timestamp = double(n) / config_.fps;
        ++frame_count_;
        return f;
    }

    void close() override {}

    DeviceInfo device_info() const override { return fake_device_info(); }

private:
    CameraConfig config_;
    uint64_t frame_count_ = 0;
    Clock::time_point start_;
};

}  // namespace

std::unique_ptr<CameraBackend> make_fake_backend() {
    return std::make_unique<FakeBackend>();
}

}  // namespace aliveness
