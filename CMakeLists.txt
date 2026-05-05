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
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

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

FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
)

set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)

set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(nlohmann_json)
FetchContent_MakeAvailable(miniaudio_source)
FetchContent_MakeAvailable(rtmidi)
FetchContent_MakeAvailable(glfw)
FetchContent_MakeAvailable(imgui)

find_package(OpenGL REQUIRED)

add_library(imgui_glfw_opengl2 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
)

target_include_directories(imgui_glfw_opengl2 PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_glfw_opengl2 PUBLIC glfw OpenGL::GL)

add_executable(ilys-mt
    src/main.cpp
    src/ilys/audio/AudioEngine.cpp
    src/ilys/audio/MiniaudioBackend.cpp
    src/ilys/cli/TerminalApp.cpp
    src/ilys/dsp/InstrumentMonitorProcessor.cpp
    src/ilys/midi/RtMidiBackend.cpp
    src/ilys/presets/PresetManager.cpp
    src/ilys/project/ProjectManager.cpp
)

target_include_directories(ilys-mt PRIVATE src)
target_link_libraries(ilys-mt PRIVATE miniaudio rtmidi nlohmann_json::nlohmann_json)

add_executable(ilys-audiovisualizer
    audiovisualizer/src/main.cpp
    src/ilys/midi/RtMidiBackend.cpp
)

target_include_directories(ilys-audiovisualizer PRIVATE src)
target_compile_definitions(ilys-audiovisualizer PRIVATE GL_SILENCE_DEPRECATION)
target_link_libraries(ilys-audiovisualizer PRIVATE
    miniaudio
    rtmidi
    imgui_glfw_opengl2
)

if(APPLE)
    target_link_libraries(ilys-mt PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework AudioUnit"
    )
    target_link_libraries(ilys-audiovisualizer PRIVATE
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework AudioUnit"
    )
elseif(WIN32)
    target_link_libraries(ilys-mt PRIVATE ole32 uuid winmm avrt)
    target_link_libraries(ilys-audiovisualizer PRIVATE ole32 uuid winmm avrt)
endif()

if(MSVC)
    target_compile_options(ilys-mt PRIVATE /W4 /permissive-)
    target_compile_options(ilys-audiovisualizer PRIVATE /W4 /permissive-)
else()
    target_compile_options(ilys-mt PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(ilys-audiovisualizer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET ilys-mt POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_SOURCE_DIR}/presets"
            "$<TARGET_FILE_DIR:ilys-mt>/presets"
)
