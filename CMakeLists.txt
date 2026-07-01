cmake_minimum_required(VERSION 3.20)
project(ozz_rendering_app)

set(CMAKE_CXX_STANDARD 23)

# if debug build define OZZ_DEBUG, it needs to affect ifdefs in the code
# (matches the consuming Lights engine; needed so debug asserts fire in standalone builds)
if (CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DOZZ_DEBUG)
endif ()

add_subdirectory(ozz_rendering)
