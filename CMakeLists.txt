cmake_minimum_required(VERSION 3.24)
project(mdpad VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put all build artifacts in predictable locations
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(FetchContent)

# --- SDL3 ---
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.8
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(SDL3)

# --- SDL3_ttf ---
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG        release-3.2.2
    GIT_SHALLOW    TRUE
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(SDL3_ttf)

# --- md4c ---
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    md4c
    GIT_REPOSITORY https://github.com/mity/md4c.git
    GIT_TAG        release-0.5.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(md4c)

# --- mdpad executable ---
add_executable(mdpad
    src/main.cpp
    src/app.cpp
    src/markdown_parser.cpp
    src/renderer.cpp
    src/syntax_highlight.cpp
    src/html_export.cpp
)

# Search for shared libraries next to the executable at runtime so the
# binary can be installed alongside bundled SDL3/SDL3_ttf libs.
set_target_properties(mdpad PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN"
)

target_include_directories(mdpad PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${md4c_SOURCE_DIR}/src
)

target_link_libraries(mdpad PRIVATE
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    md4c
)

# Copy assets to build directory
add_custom_command(TARGET mdpad POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:mdpad>/assets"
    COMMENT "Copying assets to build directory"
)
