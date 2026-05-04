#pragma once

#include "ilys/presets/GuitarPreset.hpp"

#include <array>
#include <atomic>
#include <cstdint>

namespace ilys::dsp {

class GuitarMonitorProcessor {
public:
    void prepare(float sampleRate, unsigned int outputChannels);
    void applyPreset(const presets::GuitarPreset& preset);

    void process(const float* input,
                 unsigned int inputChannels,
                 float* output,
                 unsigned int outputChannels,
                 std::uint32_t frameCount) noexcept;

private:
    static constexpr unsigned int maxChannels{8};

    std::atomic<unsigned int> inputChannel_{0};
    std::atomic<float> inputGain_{1.0F};
    std::atomic<float> outputGain_{0.5F};
    std::atomic<float> highPassHz_{80.0F};
    std::atomic<float> gateThreshold_{0.000316F};
    std::atomic<float> drive_{1.0F};

    float sampleRate_{48000.0F};
    unsigned int preparedChannels_{2};
    std::array<float, maxChannels> previousInput_{};
    std::array<float, maxChannels> previousOutput_{};
};

} // namespace ilys::dsp

