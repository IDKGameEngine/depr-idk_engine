project(idkengine_assets)

add_library(idkengine_assets STATIC src/ModelImporter.cpp src/ImageDecode.cpp src/PrimitiveShape.cpp src/ModelExporter.cpp)
target_compile_features(idkengine_assets PUBLIC cxx_std_23)
target_include_directories(idkengine_assets PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
# cgltf is C and the image decoder uses explicit arena allocation callbacks.
target_include_directories(idkengine_assets SYSTEM PRIVATE
    "${IDK_POLY_DIR}/idk_build/thirdparty/cgltf"
    "${IDK_POLY_DIR}/idk_build/thirdparty/assimp/contrib/stb")
target_include_directories(idkengine_assets PUBLIC "${IDK_POLY_DIR}/libidk/include")
target_link_libraries(idkengine_assets PUBLIC glm::glm-header-only)
target_compile_options(idkengine_assets PRIVATE -fno-exceptions)

option(IDK_BUILD_ASSET_TESTS "Build CPU model importer integration checks" OFF)
if(IDK_BUILD_ASSET_TESTS)
    enable_testing()
    add_executable(idk_asset_tests tests/ModelImport.cpp)
    target_link_libraries(idk_asset_tests PRIVATE idkengine_assets)
    target_compile_options(idk_asset_tests PRIVATE -fno-exceptions)
    target_compile_definitions(idk_asset_tests PRIVATE IDK_ASSET_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
    add_test(NAME idk_asset_tests COMMAND idk_asset_tests)
endif()

# Keep the demo asset in source control; idk_content/content is unpacked/ignored.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/examples/instanced-cubes.glb"
    DESTINATION "${CMAKE_INSTALL_PREFIX}/content/model/sample")
