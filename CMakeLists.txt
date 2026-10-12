cmake_minimum_required(VERSION 3.20)
project(aliveness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# --- C++ core (no Python dependency) ---------------------------------------
add_library(aliveness_core STATIC
    src/camera.cpp
    src/device_check.cpp
    src/fake_backend.cpp
)
target_include_directories(aliveness_core PUBLIC include)
set_target_properties(aliveness_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
    target_sources(aliveness_core PRIVATE
        src/win/mf_backend.cpp
        src/win/device_query.cpp
    )
    target_compile_definitions(aliveness_core PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(aliveness_core PRIVATE
        mfplat mf mfreadwrite mfuuid ole32 cfgmgr32)
endif()

if(MSVC)
    target_compile_options(aliveness_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(aliveness_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Python extension: aliveness._core -------------------------------------
pybind11_add_module(_core bindings/module.cpp)
target_link_libraries(_core PRIVATE aliveness_core)

install(TARGETS _core DESTINATION aliveness)
