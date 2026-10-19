project(idkengine_common)

add_library(
    ${PROJECT_NAME} OBJECT
)
target_compile_features(
    ${PROJECT_NAME} PUBLIC cxx_std_23
)
target_include_directories(
    ${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC SDL3 SDL3_net SDL3_mixer libidk
)
target_sources(
    ${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/AudioManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/EventManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/InputManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/PlatformManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/VideoManager.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EngineAPI.cpp"
)

set_source_files_properties(src/platform/VideoManager.cpp PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
