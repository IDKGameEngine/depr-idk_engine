# CMAKE_PREFIX_PATH supplies installation prefixes (including their bin dirs).
# IDK_SLANGC can explicitly select a compiler outside those prefixes or PATH.
include_guard(GLOBAL)

find_program(IDK_SLANGC NAMES slangc)
if(NOT IDK_SLANGC)
    message(FATAL_ERROR
        "Slang shader compiler (slangc) was not found or is not executable.\n"
        "From the workspace root inside steamrt4, run:\n"
        "  ./dep-build.sh vulkan-sdk\n"
        "If slangc is already installed, repair its executable permission with:\n"
        "  chmod u+x idk_build/local/bin/slangc\n"
        "For a custom installation, pass -DIDK_SLANGC=/absolute/path/to/slangc.")
endif()

function(idk_compile_slang output source entry stage)
    get_filename_component(source "${source}" ABSOLUTE)
    get_filename_component(output_dir "${output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
        COMMAND "${IDK_SLANGC}" "${source}"
            -entry "${entry}" -stage "${stage}"
            -target glsl -profile glsl_460 -matrix-layout-column-major
            -line-directive-mode none
            -I "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../include"
            -depfile "${output}.d" -o "${output}"
        DEPENDS "${source}"
        DEPFILE "${output}.d"
        VERBATIM
    )
endfunction()
