#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "aliveness/camera.hpp"
#include "aliveness/device_check.hpp"

namespace py = pybind11;
using namespace pybind11::literals;
using namespace aliveness;

namespace {

std::vector<py::ssize_t> frame_shape(const Frame& f) { return {f.height, f.width, 3}; }
std::vector<py::ssize_t> frame_strides(const Frame& f) { return {py::ssize_t(f.width) * 3, 3, 1}; }

}  // namespace

PYBIND11_MODULE(_core, m) {
    m.doc() = "aliveness C++ core: camera capture and device checks";

    // pybind11 tries the most recently registered translator first, so the
    // derived exceptions must be registered after the base.
    auto& camera_error =
        py::register_exception<CameraError>(m, "CameraError", PyExc_RuntimeError);
    auto& not_found_error =
        py::register_exception<CameraNotFoundError>(m, "CameraNotFoundError", camera_error.ptr());
    auto& busy_error =
        py::register_exception<CameraBusyError>(m, "CameraBusyError", camera_error.ptr());
    camera_error.attr("__doc__") = "Base class for all camera errors.";
    not_found_error.attr("__doc__") =
        "No camera at the requested index, nothing connected, or the camera was unplugged.";
    busy_error.attr("__doc__") =
        "The camera exists but cannot be used now: another app holds it, or Windows "
        "privacy settings block camera access.";

    py::class_<DeviceInfo>(m, "DeviceInfo", R"doc(
Identity of a camera and the result of the physical/virtual device check.

Returned by list_cameras() and Camera.device_info(). is_physical is a hint,
not a security boundary: it is based on what the drivers report.
)doc")
        .def_readonly("name", &DeviceInfo::name, "Human-readable name, e.g. 'Integrated Camera'.")
        .def_readonly("path", &DeviceInfo::path,
                      "OS device path (Windows: Media Foundation symbolic link).")
        .def_readonly("bus", &DeviceInfo::bus,
                      "Bus the device sits on: 'usb', 'pci', 'swd', 'root', ... ('' if unknown).")
        .def_readonly("driver", &DeviceInfo::driver,
                      "Driver service name, e.g. 'usbvideo' ('' if unknown).")
        .def_readonly("is_physical", &DeviceInfo::is_physical,
                      "True if the device looks like real camera hardware.")
        .def_readonly("reason", &DeviceInfo::reason, "Why is_physical was decided.")
        .def("__repr__", [](const DeviceInfo& d) {
            return "<DeviceInfo name='" + d.name + "' bus='" + d.bus + "' driver='" + d.driver +
                   "' is_physical=" + (d.is_physical ? "True" : "False") + " (" + d.reason + ")>";
        });

    // Frame exposes its pixels without copying, two ways:
    //   frame.image       -> numpy array whose .base is the Frame
    //   np.asarray(frame) -> via the buffer protocol
    // Either way the Frame (and its pixel buffer) stays alive as long as the
    // array does. Buffers are never reused, so held frames are never overwritten.
    py::class_<Frame, std::shared_ptr<Frame>>(m, "Frame", py::buffer_protocol(), R"doc(
One captured frame: RGB pixels plus sequence number and timestamps.

The pixels are available without copying, as frame.image or np.asarray(frame).
They stay valid as long as you hold the frame or an array made from it; the
camera never reuses a frame's memory.
)doc")
        .def_buffer([](Frame& f) {
            return py::buffer_info(f.data.data(), sizeof(uint8_t),
                                   py::format_descriptor<uint8_t>::format(), 3, frame_shape(f),
                                   frame_strides(f));
        })
        .def_property_readonly("image",
                               [](const std::shared_ptr<Frame>& f) {
                                   return py::array_t<uint8_t>(frame_shape(*f), frame_strides(*f),
                                                               f->data.data(), py::cast(f));
                               },
                               "(H, W, 3) uint8 RGB numpy view of the pixels (no copy).")
        .def_readonly("width", &Frame::width, "Width in pixels.")
        .def_readonly("height", &Frame::height, "Height in pixels.")
        .def_readonly("seq", &Frame::seq, "Frame number since the camera was opened (0, 1, ...).")
        .def_readonly("timestamp", &Frame::timestamp,
                      "Capture time in seconds, device clock (from the camera driver).")
        .def_readonly("host_time", &Frame::host_time,
                      "Arrival time in seconds, host monotonic clock.")
        .def("__repr__", [](const Frame& f) {
            return "<Frame seq=" + std::to_string(f.seq) + " " + std::to_string(f.width) + "x" +
                   std::to_string(f.height) + " t=" + std::to_string(f.timestamp) + ">";
        });

    py::class_<Camera>(m, "Camera", R"doc(
An open camera. The device is opened on construction; use it as a context
manager, or call close(), to release it for other applications.

    with aliveness.Camera(0, width=640, height=480, fps=30) as cam:
        frame = cam.read()
)doc")
        .def(py::init([](int index, int width, int height, int fps, const std::string& backend) {
                 return std::make_unique<Camera>(index, CameraConfig{width, height, fps}, backend);
             }),
             "index"_a = 0, "width"_a = 640, "height"_a = 480, "fps"_a = 30, "backend"_a = "auto",
             py::call_guard<py::gil_scoped_release>(), R"doc(
Open camera number `index` (as in list_cameras()) with an exact mode.

width, height and fps must match a mode the camera supports; otherwise
CameraError is raised listing the supported modes. backend is 'auto' (the
platform camera API) or 'fake' (test pattern, no camera needed).

Raises CameraNotFoundError if there is no such camera, CameraBusyError if it
is in use or blocked by privacy settings.
)doc")
        .def("read", &Camera::read, py::call_guard<py::gil_scoped_release>(), R"doc(
Block until the next frame arrives and return it as a Frame.

Other Python threads keep running while this waits. Raises CameraError if
the camera is closed, CameraNotFoundError if it was unplugged.
)doc")
        .def("close", &Camera::close, py::call_guard<py::gil_scoped_release>(),
             "Release the camera. Safe to call twice; waits for a read() in progress.")
        .def("device_info", &Camera::device_info,
             "Return this camera's DeviceInfo (still available after close()).")
        .def_property_readonly("is_open", &Camera::is_open, "True until close() is called.")
        .def_property_readonly(
            "width", [](const Camera& c) { return c.config().width; }, "Frame width in pixels.")
        .def_property_readonly(
            "height", [](const Camera& c) { return c.config().height; }, "Frame height in pixels.")
        .def_property_readonly(
            "fps", [](const Camera& c) { return c.config().fps; }, "Requested frames per second.")
        .def("__enter__", [](Camera& c) -> Camera& { return c; }, py::return_value_policy::reference)
        .def("__exit__", [](Camera& c, const py::args&) { c.close(); });

    m.def("list_cameras", &list_cameras, "backend"_a = "auto",
          py::call_guard<py::gil_scoped_release>(), R"doc(
Return a list of DeviceInfo, one per camera. The list index is the index to
pass to Camera(). backend is 'auto' or 'fake'.
)doc");

    // Exposed for unit tests of the classification rules.
    m.def(
        "_classify_device",
        [](const std::string& name, const std::string& path, const std::string& enumerator,
           const std::string& driver) {
            DeviceClass c = classify_device(name, path, enumerator, driver);
            return py::make_tuple(c.bus, c.is_physical, c.reason);
        },
        "name"_a, "path"_a, "enumerator"_a = "", "driver"_a = "",
        "Test hook: classify_device() as a (bus, is_physical, reason) tuple.");
}
