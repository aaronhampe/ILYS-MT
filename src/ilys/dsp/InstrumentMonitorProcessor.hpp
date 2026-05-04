#pragma once

#include "ilys/presets/InstrumentPreset.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace ilys::dsp {

class InstrumentMonitorProcessor {
public:
    enum class Waveform : int {
        Sine = 0,
        Triangle = 1,
        Saw = 2,
        Square = 3
    };

    void prepare(float sampleRate, unsigned int outputChannels);
    void applyPreset(const presets::InstrumentPreset& preset);
    [[nodiscard]] bool requiresAudioInput() const noexcept;

    void noteOn(unsigned int note, float velocity) noexcept;
    void noteOff(unsigned int note) noexcept;

    void process(const float* input,
                 unsigned int inputChannels,
                 float* output,
                 unsigned int outputChannels,
                 std::uint32_t frameCount) noexcept;

private:
    static constexpr unsigned int maxChannels{8};
    static constexpr unsigned int maxVoices{24};

    enum class SourceMode : int {
        Audio = 0,
        Midi = 1
    };

    enum class EnvelopeStage {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct Voice {
        std::atomic<bool> gate{false};
        std::atomic<unsigned int> note{60};
        std::atomic<float> velocity{0.0F};
        unsigned int activeNote{60};
        float phase{0.0F};
        float detunePhase{0.0F};
        float envelope{0.0F};
        float releaseStart{0.0F};
        EnvelopeStage stage{EnvelopeStage::Idle};
    };

    std::atomic<int> sourceMode_{static_cast<int>(SourceMode::Audio)};
    std::atomic<unsigned int> inputChannel_{0};
    std::atomic<float> inputGain_{1.0F};
    std::atomic<float> outputGain_{0.5F};
    std::atomic<float> highPassHz_{80.0F};
    std::atomic<float> lowPassHz_{18000.0F};
    std::atomic<float> gateThreshold_{0.000316F};
    std::atomic<float> drive_{1.0F};
    std::atomic<int> waveform_{static_cast<int>(Waveform::Sine)};
    std::atomic<unsigned int> activeVoiceLimit_{12};
    std::atomic<float> attackMs_{5.0F};
    std::atomic<float> decayMs_{80.0F};
    std::atomic<float> sustain_{0.75F};
    std::atomic<float> releaseMs_{180.0F};
    std::atomic<float> tone_{0.5F};
    std::atomic<float> detuneCents_{0.0F};
    std::atomic<float> stereoWidth_{0.35F};
    std::atomic<float> tremoloDepth_{0.0F};
    std::atomic<float> tremoloRateHz_{5.0F};
    std::atomic<unsigned int> nextVoiceToSteal_{0};

    float sampleRate_{48000.0F};
    unsigned int preparedChannels_{2};
    float tremoloPhase_{0.0F};
    std::array<float, maxChannels> previousHighPassInput_{};
    std::array<float, maxChannels> previousHighPassOutput_{};
    std::array<float, maxChannels> previousLowPassOutput_{};
    std::array<Voice, maxVoices> voices_{};
};

} // namespace ilys::dsp
