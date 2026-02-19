cmake_minimum_required(VERSION 3.20)

project(ozz_vk)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)
find_package(Vulkan REQUIRED COMPONENTS glslang)


FetchContent_Declare(
        volklib
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.4.341.0
)

FetchContent_MakeAvailable(volklib)

set(SOURCES
        src/glslang/resources.cpp

        src/util.cpp
        src/core.cpp
        src/physical_devices.cpp
        src/vulkan_queue.cpp
        src/vulkan_shader.cpp
)
add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        Vulkan::glslang
        spdlog
        volk
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        include/
)
