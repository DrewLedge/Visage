cmake_minimum_required(VERSION 3.2)
project(Visage VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# require vulkan
find_package(Vulkan REQUIRED)

include(FetchContent)

# glfw
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# imgui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PRIVATE Vulkan::Vulkan glfw)

# stb image
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# tinygltf
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG v2.9.5
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tinygltf)

# engine subdir
add_subdirectory(engine)
