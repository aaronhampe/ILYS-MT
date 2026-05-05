# ILYS-MT Architecture

ILYS-MT starts as a terminal-first audio workstation, but the code is split along the boundaries a larger DAW will need.

## Current Layers

- `cli`: terminal shell, commands, and user-facing text
- `project`: project discovery, creation, and filesystem layout
- `audio`: device discovery, audio stream lifetime, backend integration
- `dsp`: real-time audio processing
- `presets`: loading JSON presets from instrument category folders
- `core`: shared small types that do not depend on audio or UI

## Command Model

ILYS-MT starts in a workspace command mode. This mode is intentionally limited to help, project management, device setup, audiovisualizer folder creation, and quitting:

```text
/help
/create project "name"
/open project "name"
/projects
/audiovisualizer
/input [index]
/output [index]
/midi input [index]
/devices
/quit
```

Creating or opening a project switches the terminal into project mode. Live monitoring, preset loading, status, and future region-editing commands are only available there.

Projects live under `projects/<name>` with starter `audio`, `midi`, `regions`, and `mixes` folders plus `project.json` metadata.

Project mode owns the region workflow:

```text
/create region "name"
/select region "name"
/regions
/bpm <value>
/key <name>
/record
/play
/loop start
/loop stop
/import "path"
/delete recording
/mute region
/preset region <category> <id>
```

Recording is currently a mono clip captured from the monitored instrument signal. Region metadata is stored in `regions/regions.json`, recorded clips are stored as float WAV files under `audio/`, and playback mixes every unmuted recorded region from the beginning at the same time.

The project command surface now covers the core tracking loop found in most DAWs: transport-style play/record/stop, loop playback, input monitoring, tempo and key metadata, region import/clear, overwrite protection, per-region mute, and per-region instrument preset assignment. More advanced DAW features such as punch in/out, comping/takes, automation, markers, metronome, editing tools, and time-stretching should wait for a real transport and timeline model.

## Audio Model

The first engine mode is live instrument monitoring:

```text
audio input -> instrument monitor processor -> audio output
MIDI input -> internal instrument voices -> audio output
```

The application does not listen to raw USB ports. USB audio interfaces are exposed by the operating system as audio devices, so ILYS-MT discovers them through the cross-platform audio backend.

USB keyboards and synths may expose either audio, MIDI, or both. ILYS-MT now has a MIDI input layer for controllers and a starter internal voice engine for piano and synth presets.

## Real-Time Rules

Code called from the audio callback should avoid:

- heap allocation
- file I/O
- locks
- logging
- exceptions

Preset changes are copied into atomics on the processor so the callback can keep running without waiting on the terminal thread.

## Growth Path

Near-term additions should fit into these boundaries:

- track/session model in a new `src/ilys/session` layer
- transport and clock in a new `src/ilys/transport` layer
- plugin/effect graph behind the existing `dsp` boundary
- richer MIDI event routing in the existing `src/ilys/midi` layer
- dedicated software instruments in a future `src/ilys/instruments` layer
- recording and file import/export in a dedicated storage layer
- GUI later as another frontend beside `cli`, not as a replacement for the engine
