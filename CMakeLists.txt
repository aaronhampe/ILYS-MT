cmake_minimum_required(VERSION 3.21)

project(ILYS-MT
    VERSION 0.1.0
    DESCRIPTION "A terminal-first, cross-platform music workstation"
    LANGUAGES CXX
)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_Declare(
    miniaudio_source
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
)

set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json)
FetchContent_MakeAvailable(miniaudio_source)

add_executable(ilys-mt
    src/main.cpp
    src/ilys/audio/AudioEngine.cpp
    src/ilys/audio/MiniaudioBackend.cpp
    src/ilys/cli/TerminalApp.cpp
    src/ilys/dsp/GuitarMonitorProcessor.cpp
    src/ilys/presets/PresetManager.cpp
)

target_include_directories(ilys-mt PRIVATE src)
target_link_libraries(ilys-mt PRIVATE miniaudio nlohmann_json::nlohmann_json)

if(APPLE)
    target_link_libraries(ilys-mt PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework AudioUnit"
    )
elseif(WIN32)
    target_link_libraries(ilys-mt PRIVATE ole32 uuid winmm avrt)
endif()

if(MSVC)
    target_compile_options(ilys-mt PRIVATE /W4 /permissive-)
else()
    target_compile_options(ilys-mt PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET ilys-mt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/presets"
            "$<TARGET_FILE_DIR:ilys-mt>/presets"
)
