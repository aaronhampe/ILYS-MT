#pragma once

#include "ilys/audio/AudioDevice.hpp"
#include "ilys/core/Result.hpp"
#include "ilys/dsp/InstrumentMonitorProcessor.hpp"
#include "ilys/presets/InstrumentPreset.hpp"

#include <memory>
#include <optional>
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
    void stop();

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace ilys::audio
