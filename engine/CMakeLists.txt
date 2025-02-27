# ----------------------------------------
# SHADERS
# ----------------------------------------

# shader dirs
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${SHADER_DIR}/compiled)
set(SHADER_INCLUDE_DIR ${SHADER_DIR}/includes)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

# shader files
set(SHADERS
    ${SHADER_DIR}/pathtracing/gen.rgen
    ${SHADER_DIR}/pathtracing/closehit.rchit
    ${SHADER_DIR}/pathtracing/anyhit.rahit
    ${SHADER_DIR}/pathtracing/miss.rmiss
    ${SHADER_DIR}/pathtracing/shadowhit.rchit
    ${SHADER_DIR}/pathtracing/shadowanyhit.rahit
    ${SHADER_DIR}/pathtracing/shadowmiss.rmiss
    ${SHADER_DIR}/pathtracing/present.vert
    ${SHADER_DIR}/pathtracing/present.frag
    ${SHADER_DIR}/rasterization/composition.vert
    ${SHADER_DIR}/rasterization/composition.frag
    ${SHADER_DIR}/rasterization/lighting.vert
    ${SHADER_DIR}/rasterization/lighting.frag
    ${SHADER_DIR}/rasterization/wboit.vert
    ${SHADER_DIR}/rasterization/wboit.frag
    ${SHADER_DIR}/rasterization/sky.vert
    ${SHADER_DIR}/rasterization/sky.frag
    ${SHADER_DIR}/rasterization/deferred.vert
    ${SHADER_DIR}/rasterization/deferred.frag
    ${SHADER_DIR}/rasterization/shadow.vert
    ${SHADER_DIR}/rasterization/shadow.frag
)

foreach(SHADER IN LISTS SHADERS)
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE) # get name of shader file
    get_filename_component(SHADER_EXT ${SHADER} EXT) # get extension of shader file
    set(SHADER_OUT ${SHADER_OUT_DIR}/${SHADER_NAME}${SHADER_EXT}.spv)

    # custom command to compile shaders
    add_custom_command(
        OUTPUT ${SHADER_OUT}
        COMMAND glslc --target-env=vulkan1.3 -O ${SHADER} -I ${SHADER_INCLUDE_DIR} -o ${SHADER_OUT}
        DEPENDS ${SHADER}
    )
    list(APPEND COMPILED_SHADERS ${SHADER_OUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${COMPILED_SHADERS})

add_executable(${PROJECT_NAME}
    main.cpp
    src/visage.cpp
    src/internal/vk-setup.cpp
    src/internal/vk-swapchain.cpp
    src/internal/vk-textures.cpp
    src/internal/vk-scene.cpp
    src/internal/vk-buffers.cpp
    src/internal/vk-descriptorsets.cpp
    src/internal/vk-pipelines.cpp
    src/internal/vk-raytracing.cpp
    src/internal/vk-renderer.cpp
    src/libraries/dvl.cpp
    src/libraries/vkhelper.cpp
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# include dirs
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# link the libraries
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Vulkan::Vulkan
        glfw
        imgui
        tinygltf
)

target_compile_definitions(${PROJECT_NAME} PRIVATE PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
