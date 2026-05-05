#pragma once

#include "ilys/audio/AudioDevice.hpp"
#include "ilys/core/Result.hpp"
#include "ilys/dsp/InstrumentMonitorProcessor.hpp"
#include "ilys/presets/InstrumentPreset.hpp"

#include <memory>
#include <optional>
#include <filesystem>
#include <string>
#include <vector>

namespace ilys::audio {

struct AudioSettings {
    std::optional<unsigned int> inputDeviceIndex;
    std::optional<unsigned int> outputDeviceIndex;
    unsigned int sampleRate{48000};
    unsigned int framesPerBuffer{256};
    unsigned int inputChannels{1};
    unsigned int outputChannels{2};
};

struct AudioClip {
    std::vector<float> samples;
    unsigned int sampleRate{48000};
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] std::vector<AudioDevice> listInputDevices() const;
    [[nodiscard]] std::vector<AudioDevice> listOutputDevices() const;
    [[nodiscard]] const AudioSettings& settings() const noexcept;
    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] bool requiresAudioInput() const noexcept;

    core::Result setInputDevice(unsigned int index);
    core::Result setOutputDevice(unsigned int index);
    core::Result applyPreset(const presets::InstrumentPreset& preset);
    void noteOn(unsigned int note, float velocity) noexcept;
    void noteOff(unsigned int note) noexcept;
    core::Result start();
    core::Result beginRecording(double bpm,
                                bool metronomeEnabled,
                                unsigned int countInBeats,
                                double maxSeconds = 300.0);
    AudioClip finishRecording();
    core::Result playClips(std::vector<AudioClip> clips, bool loop = false, bool monitorInput = false);
    core::Result loadClipFromFile(const std::filesystem::path& path, AudioClip& clip);
    void stopPlayback() noexcept;
    void stop();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ilys::audio
