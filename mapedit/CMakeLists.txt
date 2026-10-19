project(mapedit)

add_library(idkmapedit_document STATIC src/MapSession.cpp)
target_compile_features(idkmapedit_document PUBLIC cxx_std_23)
target_include_directories(idkmapedit_document PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(idkmapedit_document PUBLIC idkeditor_shapes)
target_compile_options(idkmapedit_document PRIVATE -fno-exceptions)

add_executable(mapedit src/Main.cpp src/MapEditor.cpp src/PlaytestProcess.cpp)
target_compile_features(mapedit PRIVATE cxx_std_23)
target_compile_options(mapedit PRIVATE -fno-exceptions -Wno-missing-field-initializers)
target_link_libraries(mapedit PRIVATE idkengine idkmapedit_document idkeditor_viewport idklevel_build)
target_compile_definitions(mapedit PRIVATE IDK_PLAYTEST_EXECUTABLE="$<TARGET_FILE_NAME:${IDK_APP_NAME}>")
add_dependencies(mapedit ${IDK_APP_NAME} idk_playtest_example)

if(IDK_BUILD_EDITOR_TESTS)
    enable_testing()
    add_executable(idk_mapedit_tests tests/MapSession.cpp)
    target_compile_options(idk_mapedit_tests PRIVATE -fno-exceptions)
    target_link_libraries(idk_mapedit_tests PRIVATE idkmapedit_document)
    add_test(NAME idk_mapedit_tests COMMAND idk_mapedit_tests)
endif()

install(TARGETS mapedit DESTINATION "${CMAKE_INSTALL_PREFIX}")
