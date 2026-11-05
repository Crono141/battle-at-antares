# Battle at Antares, WP3.0 trial path A: the game as a GDExtension library for a Godot 4 host.
# The game's own startup (src/main.cpp, renamed) runs on a thread the extension owns; see
# src/antares/host.h and notes/antares/frontend-trial.md in the Moo2Mods workspace.

set(GODOTCPP_API_VERSION "4.7" CACHE STRING "" FORCE)
set(GODOTCPP_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_SOURCE_DIR}/vendors/godot-cpp" "${CMAKE_BINARY_DIR}/vendors/godot-cpp" EXCLUDE_FROM_ALL)

add_library(antares_godot SHARED
    src/antares_game.cpp
    src/antares_game.h
    src/register_types.cpp
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    $<TARGET_OBJECTS:orion2re_game_objects>
    "${ORION2RE_VENDORED_LIBSMACKER_DIR}/smacker.c"
    "${ORION2RE_VENDORED_LIBSMACKER_DIR}/smk_bitstream.c"
    "${ORION2RE_VENDORED_LIBSMACKER_DIR}/smk_hufftree.c"
    "${ORION2RE_VENDORED_YYJSON_DIR}/src/yyjson.c"
)
# The game's main() becomes a function the extension calls on its game-host thread.
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=antares_hosted_main;SDL_MAIN_HANDLED")
target_compile_features(antares_godot PRIVATE cxx_std_20)
target_include_directories(antares_godot PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${ORION2RE_VENDORED_LIBSMACKER_DIR}"
    "${ORION2RE_VENDORED_YYJSON_DIR}/src"
)
target_link_libraries(antares_godot PRIVATE
    godot::cpp
    SDL3::SDL3-static
    uv_a
    orion2re_lz4 webpdecoder
    orion2re_stb_vorbis
    orion2re_lua
    orion2re_scalers
    miniz
    orion2re_imgui
)
set(ANTARES_GODOT_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/project/bin" CACHE PATH
    "Where libantares.so goes (the trial project's bin/ by default)")
set_target_properties(antares_godot PROPERTIES
    OUTPUT_NAME antares
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${ANTARES_GODOT_BIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${ANTARES_GODOT_BIN_DIR}"
)
if(WIN32)
    set_target_properties(antares_godot PROPERTIES PREFIX "")
endif()
