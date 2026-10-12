#pragma once

// Internal: constructors for the concrete backends.

#include <memory>

#include "aliveness/camera.hpp"

namespace aliveness {

std::unique_ptr<CameraBackend> make_fake_backend();

#ifdef _WIN32
std::unique_ptr<CameraBackend> make_mf_backend();
#endif

}  // namespace aliveness
