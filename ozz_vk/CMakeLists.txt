cmake_minimum_required(VERSION 3.20)

project(ozz_vk)

set(CMAKE_CXX_STANDARD 23)
find_package(Vulkan REQUIRED)

set(SOURCES
        src/util.cpp
        src/core.cpp
        src/physical_devices.cpp
        src/vulkan_queue.cpp
)
add_library(${PROJECT_NAME} STATIC ${SOURCES})

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        Vulkan::Vulkan
        spdlog
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        include/
)
