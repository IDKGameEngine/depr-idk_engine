project(idkengine_gfx)

find_package(OpenGL REQUIRED)

add_library(
    ${PROJECT_NAME} OBJECT
)
target_compile_features(
    ${PROJECT_NAME} PUBLIC cxx_std_23
)
target_compile_options(
    ${PROJECT_NAME} PRIVATE -fno-exceptions -Wno-missing-field-initializers -Wno-unused-parameter
)
target_include_directories(
    ${PROJECT_NAME}
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC IdkThirdParty::glad OpenGL::GL idkengine_common idkengine_assets
)
target_sources(
    ${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/RenderEngine.cpp"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Slang.cmake")
set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
idk_compile_slang("${shader_dir}/default.vert.glsl" shaders/default.slang vertmain vertex)
idk_compile_slang("${shader_dir}/default.frag.glsl" shaders/default.slang fragmain fragment)
add_custom_command(
    OUTPUT "${shader_dir}/DefaultShaders.hpp"
    COMMAND "${CMAKE_COMMAND}"
        "-DVERTEX=${shader_dir}/default.vert.glsl"
        "-DFRAGMENT=${shader_dir}/default.frag.glsl"
        "-DOUTPUT=${shader_dir}/DefaultShaders.hpp"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS "${shader_dir}/default.vert.glsl" "${shader_dir}/default.frag.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    VERBATIM
)
target_sources(${PROJECT_NAME} PRIVATE "${shader_dir}/DefaultShaders.hpp")
target_include_directories(${PROJECT_NAME} PRIVATE "${shader_dir}")

option(IDK_BUILD_GFX_TESTS "Build OpenGL renderer integration checks (requires a GL 4.6 context)" OFF)
if(IDK_BUILD_GFX_TESTS)
    enable_testing()
    add_executable(idk_gfx_smoke tests/RenderSmoke.cpp)
    target_link_libraries(idk_gfx_smoke PRIVATE idkengine)
    target_compile_options(idk_gfx_smoke PRIVATE -fno-exceptions)
    target_compile_definitions(idk_gfx_smoke PRIVATE IDK_ASSET_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/../asset/tests/data")
    add_test(NAME idk_gfx_smoke COMMAND idk_gfx_smoke)
endif()
