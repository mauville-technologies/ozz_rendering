project(ozz_rendering_third_party)
include(FetchContent)

find_package(Vulkan REQUIRED)

if (NOT TARGET spdlog)
    FetchContent_Declare(
            spdlog_lib
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.15.0
    )

    FetchContent_MakeAvailable(spdlog_lib)
endif ()

FetchContent_Declare(
        volklib
        GIT_REPOSITORY https://github.com/zeux/volk.git
        GIT_TAG vulkan-sdk-1.4.341.0
)

FetchContent_MakeAvailable(volklib)

FetchContent_Declare(
        vmalib
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.3.0
)
FetchContent_MakeAvailable(vmalib)

set(SPIRV_REFLECT_STATIC_LIB ON)
FetchContent_Declare(
        spirvreflect_lib
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Reflect.git
        GIT_TAG vulkan-sdk-1.4.341.0
)
FetchContent_MakeAvailable(spirvreflect_lib)

set(ENABLE_OPT OFF CACHE BOOL "Disable glslang optimizer support; runtime shader compilation does not require SPIRV-Tools-opt" FORCE)
set(ALLOW_EXTERNAL_SPIRV_TOOLS OFF CACHE BOOL "Do not look for an external SPIRV-Tools-opt package" FORCE)
FetchContent_Declare(
        glslanglib
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG vulkan-sdk-1.4.341.0
)
FetchContent_MakeAvailable(glslanglib)

add_library(${PROJECT_NAME} INTERFACE)

target_compile_definitions(${PROJECT_NAME} INTERFACE VK_NO_PROTOTYPES)
target_link_libraries(${PROJECT_NAME}
        INTERFACE
        Vulkan::Headers
        volk
        spirv-reflect-static
        spdlog
        glslang
        VulkanMemoryAllocator
)
