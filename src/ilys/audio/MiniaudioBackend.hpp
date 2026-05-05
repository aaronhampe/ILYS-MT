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
    core::Result beginRecording(double bpm,
                                bool metronomeEnabled,
                                unsigned int countInBeats,
                                double maxSeconds);
    AudioClip finishRecording();
    core::Result playClips(std::vector<AudioClip> clips, bool loop, bool monitorInput);
    core::Result loadClipFromFile(const std::filesystem::path& path, AudioClip& clip);
    void stopPlayback() noexcept;
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
    core::Result startStream(bool needsAudioInput, const std::string& successMessage);
    void mixPlayback(float* output, unsigned int outputChannels, ma_uint32 frameCount) noexcept;
    void captureInput(const float* input, unsigned int inputChannels, ma_uint32 frameCount) noexcept;
    void mixMetronome(float* output, unsigned int outputChannels, ma_uint32 frameCount) noexcept;

    ma_context context_{};
    ma_device device_{};
    bool contextReady_{false};
    bool deviceReady_{false};
    std::atomic<bool> running_{false};

    AudioSettings settings_{};
    dsp::InstrumentMonitorProcessor processor_{};
    std::atomic<bool> recording_{false};
    std::atomic<bool> playback_{false};
    std::atomic<bool> loopPlayback_{false};
    std::atomic<bool> metronome_{false};
    std::vector<float> recordingBuffer_;
    std::vector<AudioClip> playbackClips_;
    std::size_t playbackPosition_{0};
    std::size_t playbackLength_{0};
    std::size_t maxRecordingSamples_{0};
    std::size_t recordingDelaySamples_{0};
    std::size_t metronomePosition_{0};
    std::size_t metronomeBeatSamples_{24000};
    std::size_t metronomeClickSamples_{2400};
};

} // namespace ilys::audio
