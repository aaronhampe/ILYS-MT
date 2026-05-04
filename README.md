# ILYS-MT

ILYS-MT is a terminal-first C++ music workstation. The first milestone is small on purpose:

- open a terminal shell with a welcome message
- enumerate audio input and output devices
- route a guitar input through a preset-driven DSP chain to the selected output
- keep the architecture ready for a larger DAW without adding a GUI yet

## Requirements

- C++20 compiler
- CMake 3.21+
- macOS or Windows

Dependencies are fetched by CMake:

- miniaudio for cross-platform audio I/O
- nlohmann/json for preset files

## Build

macOS:

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/ilys-mt
```

Windows:

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
.\build\windows-debug\Debug\ilys-mt.exe
```

## First Run

Inside the ILYS-MT shell:

```text
devices
input 1
output 0
presets guitar
load guitar clean_di
start
stop
quit
```

Use headphones or keep your speaker volume low at first. Live monitoring can feed back if a microphone or guitar pickup hears the speakers.

