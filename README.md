# ILYS-MT

ILYS-MT is a terminal-first C++ music workstation. The first milestone is small on purpose:

- open a terminal shell with a welcome message
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

Inside the ILYS-MT shell:

```text
devices
midi
input 1
output 0
presets
presets guitar
load guitar clean_di
start
stop

midi input 0
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

## Preset Library

- `guitar`: 10 starter presets from clean DI to high-gain rhythm
- `piano`: 5 starter presets for digital piano and e-piano monitoring
- `synth`: 5 starter presets for MIDI-controlled internal synth sounds
