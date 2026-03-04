project(ozz_rendering_third_party)
include(FetchContent)


find_package(Vulkan REQUIRED COMPONENTS glslang SPIRV-Tools)
FetchContent_Declare(
        spdloglib
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.0
)
FetchContent_MakeAvailable(spdloglib)

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

add_library(${PROJECT_NAME} INTERFACE)

target_compile_definitions(${PROJECT_NAME} INTERFACE VK_NO_PROTOTYPES)
target_link_libraries(${PROJECT_NAME}
        INTERFACE
        Vulkan::Headers
        volk
        Vulkan::glslang
        Vulkan::SPIRV-Tools
        spirv-reflect-static
        spdlog
        VulkanMemoryAllocator
)