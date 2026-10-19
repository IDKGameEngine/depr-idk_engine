project(idklevel)

add_library(idklevel STATIC src/Level.cpp src/Player.cpp)
target_compile_features(idklevel PUBLIC cxx_std_23)
target_include_directories(idklevel PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(idklevel PUBLIC idkengine_assets PRIVATE Jolt::Jolt)
target_compile_options(idklevel PRIVATE -fno-exceptions)

add_library(idklevel_build STATIC src/Build.cpp)
target_link_libraries(idklevel_build PUBLIC idklevel idkmapedit_document)
target_compile_options(idklevel_build PRIVATE -fno-exceptions)

add_library(idklevel_playtest STATIC src/Playtest.cpp src/LevelRunner.cpp)
target_link_libraries(idklevel_playtest PUBLIC idklevel idkengine)
target_compile_options(idklevel_playtest PRIVATE -fno-exceptions)

add_executable(mapc src/Main.cpp)
target_link_libraries(mapc PRIVATE idklevel_build)
target_compile_options(mapc PRIVATE -fno-exceptions)
install(TARGETS mapc DESTINATION "${CMAKE_INSTALL_PREFIX}")

set(examples)
foreach(name playground finish)
    set(example "${CMAKE_CURRENT_BINARY_DIR}/${name}.idklvl")
    add_custom_command(OUTPUT "${example}"
        COMMAND $<TARGET_FILE:mapc> "${CMAKE_CURRENT_SOURCE_DIR}/examples/${name}.idkmap" "${example}" --force
        DEPENDS mapc "${CMAKE_CURRENT_SOURCE_DIR}/examples/${name}.idkmap"
        VERBATIM)
    list(APPEND examples "${example}")
    install(FILES "examples/${name}.idkmap" "${example}" DESTINATION "${CMAKE_INSTALL_PREFIX}/content/level")
endforeach()
add_custom_target(idk_playtest_example ALL DEPENDS ${examples})

if(IDK_BUILD_EDITOR_TESTS)
    enable_testing()
    add_executable(idk_level_tests tests/Level.cpp)
    target_link_libraries(idk_level_tests PRIVATE idklevel_build)
    target_compile_options(idk_level_tests PRIVATE -fno-exceptions)
    add_test(NAME idk_level_tests COMMAND idk_level_tests)
    add_executable(idk_level_runner_tests tests/LevelRunner.cpp)
    target_link_libraries(idk_level_runner_tests PRIVATE idklevel_build idklevel_playtest)
    target_compile_options(idk_level_runner_tests PRIVATE -fno-exceptions)
    add_test(NAME idk_level_runner_tests COMMAND idk_level_runner_tests)
endif()
