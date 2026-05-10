cmake_minimum_required(VERSION 3.20)
project(ozz_rendering_app)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)

FetchContent_Declare(
        glfwlib
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG master
)
FetchContent_MakeAvailable(glfwlib)

FetchContent_Declare(
        glmlib
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG master
)

FetchContent_MakeAvailable(glmlib)

target_compile_definitions(glm INTERFACE
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)

add_executable(${PROJECT_NAME} main.cpp)

add_subdirectory(ozz_rendering)

target_link_libraries(${PROJECT_NAME}
        PRIVATE
        ozz_rendering
        glfw
        glm
)

add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}_CopyAssets)
add_custom_target(${PROJECT_NAME}_CopyAssets ALL
        COMMAND ${CMAKE_COMMAND} -E echo "Copying assets..."
)

# Copy assets
set(ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)
add_custom_command(TARGET ${PROJECT_NAME}_CopyAssets PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ASSETS_DIR}/ $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets)


message(STATUS "Setting base directory for ${target} to ${OZZ_OUTPUT_BASE_DIR}/${target}")
set(OZZ_OUTPUT_BASE_DIR ${CMAKE_SOURCE_DIR}/dist/${CMAKE_SYSTEM_NAME}/${CMAKE_BUILD_TYPE})
set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${OZZ_OUTPUT_BASE_DIR}/${PROJECT_NAME}
        ARCHIVE_OUTPUT_DIRECTORY ${OZZ_OUTPUT_BASE_DIR}/${PROJECT_NAME}
)

if(OZZ_ENABLE_WEBGPU)
    add_executable(ozz_rendering_webgpu_test main_webgpu.cpp)
    target_link_libraries(ozz_rendering_webgpu_test PRIVATE ozz_rendering glfw)
    set_target_properties(ozz_rendering_webgpu_test PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${OZZ_OUTPUT_BASE_DIR}/ozz_rendering_webgpu_test
    )
    # Copy webgpu_dawn.dll and slang.dll next to the test executable at build time
    add_custom_command(TARGET ozz_rendering_webgpu_test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:webgpu_dawn>
                $<TARGET_FILE_DIR:ozz_rendering_webgpu_test>
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:slang>
                $<TARGET_FILE_DIR:ozz_rendering_webgpu_test>
    )
endif()

