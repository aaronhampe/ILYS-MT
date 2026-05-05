# Audiovisualizer

The audiovisualizer is a native GUI tuner and performance visualizer.

It listens to the default audio input and the first available MIDI input. Audio input is analyzed as a tuner signal, while MIDI note-on events update the note/frequency display immediately.

The main scene is built for social-video capture: note-colored orbit rings, reactive particles, waveform ribbons, pulsing centers, and a compact tuner overlay all respond live to guitar or MIDI input.

## Run

```sh
cmake --build --preset debug
./build/debug/ilys-audiovisualizer
```

From the ILYS-MT shell:

```text
/audiovisualizer
```

macOS may ask for microphone permission the first time the visualizer opens.
