project(ozz_rendering_third_party)
include(FetchContent)

option(OZZ_ENABLE_SLANG "Enable Slang shader compiler (required for Slang shaders on any backend)" OFF)
option(OZZ_ENABLE_WEBGPU "Enable WebGPU backend via Dawn (implies OZZ_ENABLE_SLANG)" OFF)

if(OZZ_ENABLE_WEBGPU)
    set(OZZ_ENABLE_SLANG ON CACHE BOOL "" FORCE)
endif()

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

if(OZZ_ENABLE_SLANG)
    # Slang — prebuilt binaries; source build conflicts with our glslang/spirv-tools/Dawn targets
    set(_slang_ver "2026.8.1")
    if(WIN32)
        set(_slang_archive "slang-${_slang_ver}-windows-x86_64.zip")
    else()
        set(_slang_archive "slang-${_slang_ver}-linux-x86_64.tar.gz")
    endif()
    FetchContent_Declare(
            slang_prebuilt
            URL "https://github.com/shader-slang/slang/releases/download/v${_slang_ver}/${_slang_archive}"
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(slang_prebuilt)

    add_library(slang SHARED IMPORTED GLOBAL)
    if(WIN32)
        set_target_properties(slang PROPERTIES
            IMPORTED_IMPLIB   "${slang_prebuilt_SOURCE_DIR}/lib/slang.lib"
            IMPORTED_LOCATION "${slang_prebuilt_SOURCE_DIR}/bin/slang.dll"
        )
    else()
        set_target_properties(slang PROPERTIES
            IMPORTED_LOCATION "${slang_prebuilt_SOURCE_DIR}/lib/libslang.so"
        )
    endif()
    target_include_directories(slang INTERFACE "${slang_prebuilt_SOURCE_DIR}/include")
endif()

if(OZZ_ENABLE_WEBGPU)
    # Dawn — Google's native WebGPU implementation (D3D12 + Vulkan backends only)
    set(DAWN_FETCH_DEPENDENCIES ON CACHE BOOL "" FORCE)
    set(DAWN_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(DAWN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(DAWN_BUILD_MONOLITHIC_LIBRARY OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_D3D11 OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(DAWN_ENABLE_D3D12 ON CACHE BOOL "" FORCE)
    else()
        set(DAWN_ENABLE_D3D12 OFF CACHE BOOL "" FORCE)
    endif()
    set(DAWN_ENABLE_METAL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_NULL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_DESKTOP_GL OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_OPENGLES OFF CACHE BOOL "" FORCE)
    set(DAWN_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
    # Auto-detect X11 and Wayland dev support via pkg-config.
    # X11:    sudo apt install libx11-xcb-dev   (Debian/Ubuntu)
    # Wayland: sudo apt install libwayland-dev   (Debian/Ubuntu)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(_dawn_x11xcb x11-xcb QUIET)
        if(_dawn_x11xcb_FOUND)
            set(DAWN_USE_X11 ON CACHE BOOL "" FORCE)
        else()
            set(DAWN_USE_X11 OFF CACHE BOOL "" FORCE)
            message(STATUS "Dawn: x11-xcb not found — X11 WebGPU surface disabled. Install libx11-xcb-dev to enable.")
        endif()
        pkg_check_modules(_dawn_wayland wayland-client QUIET)
        if(_dawn_wayland_FOUND)
            set(DAWN_USE_WAYLAND ON CACHE BOOL "" FORCE)
        else()
            set(DAWN_USE_WAYLAND OFF CACHE BOOL "" FORCE)
            message(STATUS "Dawn: wayland-client not found — Wayland WebGPU surface disabled. Install libwayland-dev to enable.")
        endif()
    else()
        set(DAWN_USE_X11 OFF CACHE BOOL "" FORCE)
        set(DAWN_USE_WAYLAND OFF CACHE BOOL "" FORCE)
        message(STATUS "Dawn: pkg-config not found — X11 and Wayland WebGPU surfaces disabled.")
    endif()
    # Disable extra build artifacts to reduce compile time
    set(DAWN_USE_BUILT_DXC OFF CACHE BOOL "" FORCE)
    set(TINT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(TINT_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TINT_BUILD_TINTD OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            dawn
            GIT_REPOSITORY https://dawn.googlesource.com/dawn
            GIT_TAG chromium/6736
            GIT_SHALLOW TRUE
            GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(dawn)
endif()

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

if(OZZ_ENABLE_SLANG)
    target_link_libraries(${PROJECT_NAME} INTERFACE slang)
    target_compile_definitions(${PROJECT_NAME} INTERFACE OZZ_SLANG_ENABLED)
endif()

if(OZZ_ENABLE_WEBGPU)
    target_link_libraries(${PROJECT_NAME} INTERFACE dawn_proc dawn_native)
    target_compile_definitions(${PROJECT_NAME} INTERFACE OZZ_WEBGPU_ENABLED)
endif()
