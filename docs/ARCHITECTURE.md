# ILYS-MT Architecture

ILYS-MT starts as a terminal-first audio workstation, but the code is split along the boundaries a larger DAW will need.

## Current Layers

- `cli`: terminal shell, commands, and user-facing text
- `audio`: device discovery, audio stream lifetime, backend integration
- `dsp`: real-time audio processing
- `presets`: loading JSON presets from category folders
- `core`: shared small types that do not depend on audio or UI

## Audio Model

The first engine mode is live monitoring:

```text
audio input -> guitar monitor processor -> audio output
```

The application does not listen to raw USB ports. USB audio interfaces are exposed by the operating system as audio devices, so ILYS-MT discovers them through the cross-platform audio backend.

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
- recording and file import/export in a dedicated storage layer
- GUI later as another frontend beside `cli`, not as a replacement for the engine

