# ILYS-MT

ILYS-MT is a terminal-first C++ music workstation. The first milestone is small on purpose:

- open a terminal shell with a welcome message
- create, list, and open named music projects
- enumerate audio input and output devices
- enumerate MIDI input devices
- route an instrument input through a preset-driven DSP chain to the selected output
- play internal piano and synth presets from a USB MIDI controller
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

ILYS-MT starts in the workspace shell. At this level you can manage projects and devices, but monitoring and music commands are not available until a project is open.

```text
/help
/projects
/create project "First Song"
/open project "First Song"
/input
/input 1
/output
/output 0
/midi input
/midi input 0
/devices
/audiovisualizer
/quit
```

Creating a project also opens it. Once the prompt shows the project name, the music commands are available:

```text
/create region "Verse Guitar"
/regions
/bpm 128
/key "E minor"
/preset region guitar blues_edge
/record
/play
/loop start
/loop stop
/import "/path/to/file.wav"
/delete recording
/mute region
/select region "Verse Guitar"
presets guitar
load guitar clean_di
start
stop

presets piano
load piano warm_epiano
presets synth
load synth analog_pad
start
stop
quit
```

Use headphones or keep your speaker volume low at first. Live monitoring can feed back if a microphone or guitar pickup hears the speakers.

Guitar presets process incoming audio from an audio interface. Piano and synth presets are MIDI instruments: select an audio output, select a MIDI input, load a piano or synth preset, then start monitoring.

Projects are stored under `projects/<name>` and currently contain starter folders for audio, MIDI, regions, and mixes. The `audiovisualizer` command creates the top-level `audiovisualizer` workspace for the future visualizer module.

Regions are stored in the project metadata. `/record` records the selected region from the monitored instrument signal until you press space and writes the clip to the project's `audio` folder. If the selected region already contains audio, ILYS-MT asks for overwrite confirmation. `/play` starts all unmuted recorded regions together. `/loop start` repeats the unmuted regions while keeping live input monitoring active so you can jam over the loop.

Each project stores BPM and key in `project.json`. Each region stores its own preset assignment, mute state, and optional audio file. WAV and MP3 import is handled through the audio backend and copied into the selected region as a project-local WAV.

## Preset Library

- `guitar`: 10 starter presets from clean DI to high-gain rhythm
- `piano`: 5 starter presets for digital piano and e-piano monitoring
- `synth`: 5 starter presets for MIDI-controlled internal synth sounds
