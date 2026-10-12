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
    py::register_exception<CameraNotFoundError>(m, "CameraNotFoundError", camera_error.ptr());
    py::register_exception<CameraBusyError>(m, "CameraBusyError", camera_error.ptr());

    py::class_<DeviceInfo>(m, "DeviceInfo")
        .def_readonly("name", &DeviceInfo::name)
        .def_readonly("path", &DeviceInfo::path)
        .def_readonly("bus", &DeviceInfo::bus)
        .def_readonly("driver", &DeviceInfo::driver)
        .def_readonly("is_physical", &DeviceInfo::is_physical)
        .def_readonly("reason", &DeviceInfo::reason, "Why is_physical was decided")
        .def("__repr__", [](const DeviceInfo& d) {
            return "<DeviceInfo name='" + d.name + "' bus='" + d.bus + "' driver='" + d.driver +
                   "' is_physical=" + (d.is_physical ? "True" : "False") + " (" + d.reason + ")>";
        });

    // Frame exposes its pixels without copying, two ways:
    //   frame.image       -> numpy array whose .base is the Frame
    //   np.asarray(frame) -> via the buffer protocol
    // Either way the Frame (and its pixel buffer) stays alive as long as the
    // array does. Buffers are never reused, so held frames are never overwritten.
    py::class_<Frame, std::shared_ptr<Frame>>(m, "Frame", py::buffer_protocol())
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
                               "(H, W, 3) uint8 RGB numpy view of the pixels (no copy)")
        .def_readonly("width", &Frame::width)
        .def_readonly("height", &Frame::height)
        .def_readonly("seq", &Frame::seq, "Frame number since the camera was opened (0, 1, ...)")
        .def_readonly("timestamp", &Frame::timestamp, "Seconds, device clock")
        .def_readonly("host_time", &Frame::host_time, "Seconds, host monotonic clock at arrival")
        .def("__repr__", [](const Frame& f) {
            return "<Frame seq=" + std::to_string(f.seq) + " " + std::to_string(f.width) + "x" +
                   std::to_string(f.height) + " t=" + std::to_string(f.timestamp) + ">";
        });

    py::class_<Camera>(m, "Camera")
        .def(py::init([](int index, int width, int height, int fps, const std::string& backend) {
                 return std::make_unique<Camera>(index, CameraConfig{width, height, fps}, backend);
             }),
             "index"_a = 0, "width"_a = 640, "height"_a = 480, "fps"_a = 30, "backend"_a = "auto",
             py::call_guard<py::gil_scoped_release>())
        .def("read", &Camera::read, py::call_guard<py::gil_scoped_release>(),
             "Block until the next frame arrives and return it")
        .def("close", &Camera::close, py::call_guard<py::gil_scoped_release>())
        .def("device_info", &Camera::device_info)
        .def_property_readonly("is_open", &Camera::is_open)
        .def_property_readonly("width", [](const Camera& c) { return c.config().width; })
        .def_property_readonly("height", [](const Camera& c) { return c.config().height; })
        .def_property_readonly("fps", [](const Camera& c) { return c.config().fps; })
        .def("__enter__", [](Camera& c) -> Camera& { return c; }, py::return_value_policy::reference)
        .def("__exit__", [](Camera& c, const py::args&) { c.close(); });

    m.def("list_cameras", &list_cameras, "backend"_a = "auto",
          py::call_guard<py::gil_scoped_release>(), "List cameras with their device info");

    // Exposed for unit tests of the classification rules.
    m.def(
        "_classify_device",
        [](const std::string& name, const std::string& path, const std::string& enumerator,
           const std::string& driver) {
            DeviceClass c = classify_device(name, path, enumerator, driver);
            return py::make_tuple(c.bus, c.is_physical, c.reason);
        },
        "name"_a, "path"_a, "enumerator"_a = "", "driver"_a = "");
}
