cmake_minimum_required(VERSION 3.20)
project(mycds VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

# 1. libcdio
pkg_check_modules(LIBCDIO REQUIRED IMPORTED_TARGET libcdio)

# 2. libMirage (from external/cdemu/libmirage submodule)
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
    "${CMAKE_CURRENT_SOURCE_DIR}/external/cdemu/libmirage/cmake"
)

set(GTKDOC_ENABLED OFF CACHE BOOL "Disable gtk-doc for libMirage" FORCE)
set(INTROSPECTION_ENABLED OFF CACHE BOOL "Disable GObject Introspection for libMirage" FORCE)
set(VAPI_ENABLED OFF CACHE BOOL "Disable Vala API for libMirage" FORCE)
set(POST_INSTALL_HOOKS OFF CACHE BOOL "Disable post-install hooks for libMirage" FORCE)

# Configure libMirage plugin output path so plugins load directly from the build tree
set(CMAKE_INSTALL_LIBDIR "${CMAKE_BINARY_DIR}/lib" CACHE PATH "Library directory" FORCE)
set(MIRAGE_BUILD_PLUGIN_DIR "${CMAKE_BINARY_DIR}/lib/libmirage-3.3")
set(SAVED_CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${MIRAGE_BUILD_PLUGIN_DIR}")

add_subdirectory(external/cdemu/libmirage)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${SAVED_CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
set_target_properties(mirage PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 3. SDL3
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    include(FetchContent)
    set(SDL_AUDIO ON CACHE BOOL "Enable SDL3 Audio" FORCE)
    set(SDL_VIDEO OFF CACHE BOOL "Disable SDL3 Video" FORCE)
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "Allow console-only SDL3 build" FORCE)
    set(SDL_RENDER OFF CACHE BOOL "Disable SDL3 Render" FORCE)
    set(SDL_GPU OFF CACHE BOOL "Disable SDL3 GPU" FORCE)
    set(SDL_CAMERA OFF CACHE BOOL "Disable SDL3 Camera" FORCE)
    set(SDL_JOYSTICK OFF CACHE BOOL "Disable SDL3 Joystick" FORCE)
    set(SDL_HAPTIC OFF CACHE BOOL "Disable SDL3 Haptic" FORCE)
    set(SDL_HIDAPI OFF CACHE BOOL "Disable SDL3 HIDAPI" FORCE)
    set(SDL_POWER OFF CACHE BOOL "Disable SDL3 Power" FORCE)
    set(SDL_SENSOR OFF CACHE BOOL "Disable SDL3 Sensor" FORCE)
    set(SDL_DIALOG OFF CACHE BOOL "Disable SDL3 Dialog" FORCE)
    set(SDL_TRAY OFF CACHE BOOL "Disable SDL3 Tray" FORCE)
    set(SDL_SHARED ON CACHE BOOL "Build SDL3 shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Disable SDL3 static library" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "Disable SDL3 test library" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "Disable SDL3 tests" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "Disable SDL3 examples" FORCE)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.18
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# 4. Main executable
add_executable(cdplayer
    src/main.cpp
)

if(TARGET image-toc)
    add_dependencies(cdplayer image-toc)
endif()

target_compile_options(cdplayer PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(cdplayer PRIVATE
    PkgConfig::LIBCDIO
    mirage
    SDL3::SDL3
)
