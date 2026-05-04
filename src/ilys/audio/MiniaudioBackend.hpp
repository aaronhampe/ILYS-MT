#pragma once

#include "ilys/audio/AudioDevice.hpp"
#include "ilys/audio/AudioEngine.hpp"
#include "ilys/core/Result.hpp"
#include "ilys/dsp/InstrumentMonitorProcessor.hpp"

#include <miniaudio.h>

#include <atomic>
#include <vector>

namespace ilys::audio {

class AudioEngine::Impl {
public:
    Impl();
    ~Impl();

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
    struct NativeDevice {
        AudioDevice publicInfo;
        ma_device_id nativeId{};
    };

    static void dataCallback(ma_device* device,
                             void* output,
                             const void* input,
                             ma_uint32 frameCount);

    [[nodiscard]] std::vector<NativeDevice> captureDevices() const;
    [[nodiscard]] std::vector<NativeDevice> playbackDevices() const;

    ma_context context_{};
    ma_device device_{};
    bool contextReady_{false};
    bool deviceReady_{false};
    std::atomic<bool> running_{false};

    AudioSettings settings_{};
    dsp::InstrumentMonitorProcessor processor_{};
};

} // namespace ilys::audio
