project(editor)

add_library(idkeditor_shapes STATIC src/ShapeDocument.cpp src/ShapeImport.cpp src/ShapeSession.cpp src/EditorSession.cpp)
target_compile_features(idkeditor_shapes PUBLIC cxx_std_23)
target_include_directories(idkeditor_shapes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(idkeditor_shapes PUBLIC idkengine_assets)
target_compile_options(idkeditor_shapes PRIVATE -fno-exceptions)

# The model and map editors share rendering, navigation, picking and gizmos.
add_library(idkeditor_viewport STATIC src/EditorGui.cpp src/EditorViewport.cpp src/ScenePreview.cpp)
target_compile_features(idkeditor_viewport PUBLIC cxx_std_23)
target_link_libraries(idkeditor_viewport PUBLIC idkeditor_shapes idkengine_gfx IdkThirdParty::imgui imguizmo::imguizmo)
target_compile_options(idkeditor_viewport PRIVATE -fno-exceptions -Wno-missing-field-initializers)

add_executable(
    ${PROJECT_NAME}
)
target_compile_features(
    ${PROJECT_NAME} PUBLIC cxx_std_23
)
target_compile_options(
    ${PROJECT_NAME} PRIVATE -fno-exceptions -Wno-missing-field-initializers
)
target_include_directories(
    ${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC idkengine_common idkengine_gfx idkengine_engine
    PRIVATE idkeditor_viewport
)
target_sources(
    ${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Editor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EditorUI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ImportFileDialog.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Main.cpp"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../gfx/cmake/Slang.cmake")
set(preview_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
idk_compile_slang("${preview_dir}/preview.vert.glsl" shaders/preview.slang vertmain vertex)
idk_compile_slang("${preview_dir}/preview.frag.glsl" shaders/preview.slang fragmain fragment)
add_custom_command(
    OUTPUT "${preview_dir}/PreviewShaders.hpp"
    COMMAND "${CMAKE_COMMAND}"
        "-DVERTEX=${preview_dir}/preview.vert.glsl"
        "-DFRAGMENT=${preview_dir}/preview.frag.glsl"
        "-DOUTPUT=${preview_dir}/PreviewShaders.hpp"
        "-DSHADER_NAMESPACE=idk::editor::preview"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../gfx/cmake/EmbedShaders.cmake"
    DEPENDS "${preview_dir}/preview.vert.glsl" "${preview_dir}/preview.frag.glsl"
        "${CMAKE_CURRENT_SOURCE_DIR}/../gfx/cmake/EmbedShaders.cmake"
    VERBATIM)
target_sources(idkeditor_viewport PRIVATE "${preview_dir}/PreviewShaders.hpp")
target_include_directories(idkeditor_viewport PRIVATE "${preview_dir}")

option(IDK_BUILD_EDITOR_TESTS "Build shape document and GLB export integration checks" OFF)
if(IDK_BUILD_EDITOR_TESTS)
    enable_testing()
    add_executable(idk_shape_tests tests/ShapeModel.cpp)
    target_link_libraries(idk_shape_tests PRIVATE idkeditor_shapes imguizmo::imguizmo)
    target_compile_options(idk_shape_tests PRIVATE -fno-exceptions)
    add_test(NAME idk_shape_tests COMMAND idk_shape_tests)
    add_executable(idk_editor_import_tests tests/ShapeImport.cpp)
    target_link_libraries(idk_editor_import_tests PRIVATE idkeditor_shapes)
    target_compile_options(idk_editor_import_tests PRIVATE -fno-exceptions)
    target_compile_definitions(idk_editor_import_tests PRIVATE IDK_EDITOR_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME idk_editor_import_tests COMMAND idk_editor_import_tests)
    add_executable(idk_import_dialog_tests tests/ImportFileDialog.cpp src/ImportFileDialog.cpp)
    target_compile_features(idk_import_dialog_tests PRIVATE cxx_std_23)
    target_include_directories(idk_import_dialog_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(idk_import_dialog_tests PRIVATE SDL3)
    target_compile_options(idk_import_dialog_tests PRIVATE -fno-exceptions)
    add_test(NAME idk_import_dialog_tests COMMAND idk_import_dialog_tests)
    add_executable(idk_editor_preview_tests tests/EditorPreview.cpp src/ScenePreview.cpp "${preview_dir}/PreviewShaders.hpp")
    target_compile_features(idk_editor_preview_tests PRIVATE cxx_std_23)
    target_include_directories(idk_editor_preview_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${preview_dir}")
    target_link_libraries(idk_editor_preview_tests PRIVATE idkeditor_shapes idkengine)
    target_compile_options(idk_editor_preview_tests PRIVATE -fno-exceptions)
    add_test(NAME idk_editor_preview_tests COMMAND idk_editor_preview_tests)
endif()

install(
    TARGETS ${PROJECT_NAME}
    DESTINATION "${CMAKE_INSTALL_PREFIX}"
)
