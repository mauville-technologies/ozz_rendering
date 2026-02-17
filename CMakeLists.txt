cmake_minimum_required(VERSION 3.20)
project(vulkan_playground)

set(CMAKE_CXX_STANDARD 23)

include(FetchContent)


FetchContent_Declare(
        glfwlib
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        master
)
FetchContent_MakeAvailable(glfwlib)

FetchContent_Declare(
        glmlib
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        master
)

FetchContent_MakeAvailable(glmlib)

FetchContent_Declare(
        spdloglib
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.15.0
)
FetchContent_MakeAvailable(spdloglib)

add_executable(vulkan_playground main.cpp)

add_subdirectory(ozz_vk)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ozz_vk
        glfw
        glm
        spdlog
)