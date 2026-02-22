cmake_minimum_required(VERSION 3.20)

project(ozz_rendering)

include(FetchContent)

set(CMAKE_CXX_STANDARD 23)

add_subdirectory(third_party)
set(SOURCES
        src/glslang/resources.cpp

        src/vulkan/scratch/util.cpp
        src/vulkan/scratch/core.cpp
        src/vulkan/utils/physical_devices.cpp
        src/vulkan/scratch/vulkan_queue.cpp
        src/vulkan/scratch/vulkan_shader.cpp

        src/rhi.cpp
        src/vulkan/rhi_device_vulkan.cpp
        src/vulkan/rhi_texture_vulkan.cpp
)
add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        ozz_rendering_third_party
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        include/
)
