# Audiovisualizer

The audiovisualizer is a small native GUI tuner and input visualizer.

It listens to the default audio input and the first available MIDI input. Audio input is analyzed as a tuner signal, while MIDI note-on events update the note/frequency display immediately.

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
