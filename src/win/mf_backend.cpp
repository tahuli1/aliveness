// Media Foundation capture backend (Windows).
//
// Flow:
//   MFEnumDeviceSources  -> one IMFActivate per camera
//   IMFActivate::ActivateObject -> IMFMediaSource (the device)
//   MFCreateSourceReaderFromMediaSource -> synchronous IMFSourceReader
//   pick a native mode matching width/height/fps, ask the reader for RGB32
//   output (MF inserts the MJPG decoder / YUV->RGB converter as needed),
//   then read() copies BGRA -> tightly packed RGB into a fresh Frame.

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "../backends.hpp"
#include "aliveness/device_check.hpp"
#include "device_query.hpp"

using Microsoft::WRL::ComPtr;

namespace aliveness {
namespace {

constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::string hr_hex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

// Turn an HRESULT into an exception whose message says what to do about it.
[[noreturn]] void throw_hr(const std::string& what, HRESULT hr, const std::string& device = "") {
    const std::string where = device.empty() ? what : what + " '" + device + "'";
    const std::string code = " (" + hr_hex(hr) + ")";

    if (hr == E_ACCESSDENIED) {
        throw CameraBusyError(where + ": access denied. Check Settings > Privacy & security > "
                              "Camera and allow desktop apps to use the camera" + code);
    }
    if (hr == MF_E_HW_MFT_FAILED_START_STREAMING || hr == MF_E_VIDEO_RECORDING_DEVICE_PREEMPTED ||
        hr == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION)) {
        throw CameraBusyError(where + ": camera is in use by another application. "
                              "Close it and try again" + code);
    }
    if (hr == MF_E_VIDEO_DEVICE_LOCKED) {
        throw CameraBusyError(where + ": camera is locked by the system" + code);
    }
    if (hr == MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED || hr == MF_E_SHUTDOWN) {
        throw CameraNotFoundError(where + ": camera was disconnected" + code);
    }
    throw CameraError(where + " failed" + code);
}

// COM + Media Foundation are started once per process and never shut down:
// cameras can be opened from any Python thread, and tearing MF down while a
// frame might still be in flight is not worth the risk. CoIncrementMTAUsage
// keeps a multithreaded apartment alive, so threads that never called
// CoInitialize can still use MF objects.
void ensure_mf_started() {
    static std::once_flag once;
    static HRESULT result = S_OK;
    std::call_once(once, [] {
        CO_MTA_USAGE_COOKIE cookie = nullptr;
        result = CoIncrementMTAUsage(&cookie);
        if (SUCCEEDED(result)) result = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    });
    if (FAILED(result)) throw_hr("starting Media Foundation", result);
}

std::vector<ComPtr<IMFActivate>> enum_devices() {
    ensure_mf_started();

    ComPtr<IMFAttributes> attrs;
    HRESULT hr = MFCreateAttributes(&attrs, 1);
    if (SUCCEEDED(hr)) {
        hr = attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    IMFActivate** raw = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attrs.Get(), &raw, &count);
    if (FAILED(hr)) throw_hr("enumerating cameras", hr);

    std::vector<ComPtr<IMFActivate>> devices;
    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IMFActivate> a;
        a.Attach(raw[i]);  // take ownership of the reference
        devices.push_back(std::move(a));
    }
    CoTaskMemFree(raw);
    return devices;
}

std::wstring get_string(IMFActivate* a, const GUID& key) {
    wchar_t* s = nullptr;
    UINT32 len = 0;
    if (FAILED(a->GetAllocatedString(key, &s, &len))) return L"";
    std::wstring out(s, len);
    CoTaskMemFree(s);
    return out;
}

DeviceInfo describe(IMFActivate* a) {
    DeviceInfo info;
    info.name = to_utf8(get_string(a, MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME));
    const std::wstring link = get_string(a, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
    info.path = to_utf8(link);

    const DeviceNodeProps node = query_device_node(link);
    info.driver = node.driver;
    const DeviceClass c = classify_device(info.name, info.path, node.enumerator, node.driver);
    info.bus = c.bus;
    info.is_physical = c.is_physical;
    info.reason = c.reason;
    return info;
}

// Lower is better when several native formats offer the same mode:
// uncompressed first (cheaper to convert), then MJPG.
int subtype_rank(const GUID& s) {
    if (s == MFVideoFormat_NV12) return 0;
    if (s == MFVideoFormat_YUY2) return 1;
    if (s == MFVideoFormat_MJPG) return 2;
    return 3;
}

// Copy BGRA rows (scan0 = top row, pitch may be negative) into packed RGB.
void bgra_to_rgb(const BYTE* scan0, LONG pitch, Frame& f) {
    for (int y = 0; y < f.height; ++y) {
        const BYTE* src = scan0 + ptrdiff_t(y) * pitch;
        uint8_t* dst = f.data.data() + size_t(y) * f.width * 3;
        for (int x = 0; x < f.width; ++x) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            src += 4;
            dst += 3;
        }
    }
}

class MFBackend : public CameraBackend {
public:
    ~MFBackend() override { close(); }

    std::vector<DeviceInfo> list_devices() override {
        std::vector<DeviceInfo> out;
        for (auto& a : enum_devices()) out.push_back(describe(a.Get()));
        return out;
    }

    void open(int index, const CameraConfig& config) override {
        auto devices = enum_devices();
        if (devices.empty()) {
            throw CameraNotFoundError(
                "no cameras found. Is a webcam connected and enabled in Device Manager?");
        }
        if (index < 0 || index >= int(devices.size())) {
            std::string msg = "camera index " + std::to_string(index) + " not found; " +
                              std::to_string(devices.size()) + " camera(s) available:";
            for (size_t i = 0; i < devices.size(); ++i) {
                msg += "\n  " + std::to_string(i) + ": " +
                       to_utf8(get_string(devices[i].Get(), MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME));
            }
            throw CameraNotFoundError(msg);
        }

        info_ = describe(devices[index].Get());
        try {
            HRESULT hr = devices[index]->ActivateObject(IID_PPV_ARGS(&source_));
            if (FAILED(hr)) throw_hr("opening camera", hr, info_.name);

            ComPtr<IMFAttributes> attrs;
            hr = MFCreateAttributes(&attrs, 1);
            if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
            if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromMediaSource(source_.Get(), attrs.Get(), &reader_);
            if (FAILED(hr)) throw_hr("creating reader for camera", hr, info_.name);

            select_mode(config);
        } catch (...) {
            close();
            throw;
        }
    }

    Frame read() override {
        if (!reader_) throw CameraError("camera is not open");
        for (;;) {
            DWORD stream = 0, flags = 0;
            LONGLONG timestamp = 0;  // 100 ns units
            ComPtr<IMFSample> sample;
            // Blocks until a frame arrives (sync mode has no timeout).
            HRESULT hr = reader_->ReadSample(kVideoStream, 0, &stream, &flags, &timestamp, &sample);
            if (FAILED(hr)) throw_hr("reading frame from camera", hr, info_.name);
            if (flags & MF_SOURCE_READERF_ERROR) {
                throw CameraError("camera '" + info_.name + "' reported a stream error");
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                throw CameraNotFoundError("camera '" + info_.name +
                                          "' stopped streaming (disconnected?)");
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) update_output_format();
            if (!sample) continue;  // stream tick / gap: no data, wait for the next one
            return to_frame(sample.Get(), timestamp);
        }
    }

    void close() override {
        reader_.Reset();
        if (source_) {
            source_->Shutdown();  // releases the device so other apps can use it
            source_.Reset();
        }
    }

    DeviceInfo device_info() const override { return info_; }

private:
    void select_mode(const CameraConfig& config) {
        std::vector<std::string> available;
        ComPtr<IMFMediaType> best;
        int best_rank = 1000;

        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> type;
            HRESULT hr = reader_->GetNativeMediaType(kVideoStream, i, &type);
            if (hr == MF_E_NO_MORE_TYPES) break;
            if (FAILED(hr)) throw_hr("querying modes of camera", hr, info_.name);

            UINT32 w = 0, h = 0, num = 0, den = 0;
            GUID subtype{};
            MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &num, &den);
            type->GetGUID(MF_MT_SUBTYPE, &subtype);
            const int fps = den ? int(std::lround(double(num) / den)) : 0;  // 2997/100 -> 30

            const std::string mode = std::to_string(w) + "x" + std::to_string(h) + "@" +
                                     std::to_string(fps);
            if (std::find(available.begin(), available.end(), mode) == available.end()) {
                available.push_back(mode);
            }
            if (int(w) == config.width && int(h) == config.height && fps == config.fps &&
                subtype_rank(subtype) < best_rank) {
                best = type;
                best_rank = subtype_rank(subtype);
            }
        }

        if (!best) {
            std::string msg = "camera '" + info_.name + "' does not support " +
                              std::to_string(config.width) + "x" + std::to_string(config.height) +
                              " @ " + std::to_string(config.fps) + " fps. Supported modes:";
            for (const auto& m : available) msg += " " + m;
            throw CameraError(msg);
        }

        HRESULT hr = reader_->SetCurrentMediaType(kVideoStream, nullptr, best.Get());
        if (FAILED(hr)) throw_hr("setting mode on camera", hr, info_.name);

        ComPtr<IMFMediaType> rgb;
        hr = MFCreateMediaType(&rgb);
        if (SUCCEEDED(hr)) hr = rgb->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = rgb->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = reader_->SetCurrentMediaType(kVideoStream, nullptr, rgb.Get());
        if (FAILED(hr)) throw_hr("setting RGB output for camera", hr, info_.name);

        update_output_format();
    }

    void update_output_format() {
        ComPtr<IMFMediaType> type;
        HRESULT hr = reader_->GetCurrentMediaType(kVideoStream, &type);
        if (FAILED(hr)) throw_hr("reading output format of camera", hr, info_.name);

        UINT32 w = 0, h = 0;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        width_ = int(w);
        height_ = int(h);

        // Only used if the buffer is not a 2D buffer (see to_frame).
        UINT32 stride = 0;
        stride_ = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) ? LONG(stride)
                                                                            : LONG(w * 4);
    }

    Frame to_frame(IMFSample* sample, LONGLONG timestamp) {
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) throw_hr("accessing frame buffer of camera", hr, info_.name);

        Frame f;
        f.width = width_;
        f.height = height_;
        f.data.resize(size_t(width_) * height_ * 3);
        f.timestamp = double(timestamp) / 1e7;

        // Prefer IMF2DBuffer: it hands us the top row and the real pitch,
        // which handles bottom-up images (negative pitch) for us.
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch))) {
            bgra_to_rgb(scan0, pitch, f);
            buffer2d->Unlock2D();
            return f;
        }

        // Fallback: plain buffer, stride from the media type.
        BYTE* base = nullptr;
        DWORD length = 0;
        hr = buffer->Lock(&base, nullptr, &length);
        if (FAILED(hr)) throw_hr("locking frame buffer of camera", hr, info_.name);
        const LONG abs_stride = std::abs(stride_);
        if (abs_stride < width_ * 4 || DWORD(abs_stride) * DWORD(height_) > length) {
            buffer->Unlock();
            throw CameraError("camera '" + info_.name + "' delivered a frame of unexpected size");
        }
        scan0 = stride_ < 0 ? base + ptrdiff_t(abs_stride) * (height_ - 1) : base;
        bgra_to_rgb(scan0, stride_, f);
        buffer->Unlock();
        return f;
    }

    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
    DeviceInfo info_;
    int width_ = 0;
    int height_ = 0;
    LONG stride_ = 0;
};

}  // namespace

std::unique_ptr<CameraBackend> make_mf_backend() {
    return std::make_unique<MFBackend>();
}

}  // namespace aliveness
