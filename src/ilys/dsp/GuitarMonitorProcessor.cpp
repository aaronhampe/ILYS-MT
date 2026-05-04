#include "ilys/dsp/GuitarMonitorProcessor.hpp"

#include <algorithm>
#include <cmath>

namespace ilys::dsp {
namespace {

constexpr float pi = 3.14159265358979323846F;

float dbToLinear(float valueDb) noexcept
{
    return std::pow(10.0F, valueDb / 20.0F);
}

float softClip(float value, float drive) noexcept
{
    if (drive <= 1.01F) {
        return value;
    }

    const auto driven = value * drive;
    return std::tanh(driven) / std::tanh(drive);
}

} // namespace

void GuitarMonitorProcessor::prepare(float sampleRate, unsigned int outputChannels)
{
    sampleRate_ = sampleRate > 0.0F ? sampleRate : 48000.0F;
    preparedChannels_ = std::clamp(outputChannels, 1U, maxChannels);
    previousInput_.fill(0.0F);
    previousOutput_.fill(0.0F);
}

void GuitarMonitorProcessor::applyPreset(const presets::GuitarPreset& preset)
{
    inputChannel_.store(preset.inputChannel, std::memory_order_relaxed);
    inputGain_.store(dbToLinear(preset.inputGainDb), std::memory_order_relaxed);
    outputGain_.store(dbToLinear(preset.outputGainDb), std::memory_order_relaxed);
    highPassHz_.store(std::max(10.0F, preset.highPassHz), std::memory_order_relaxed);
    gateThreshold_.store(dbToLinear(preset.gateThresholdDb), std::memory_order_relaxed);
    drive_.store(std::max(1.0F, preset.drive), std::memory_order_relaxed);
}

void GuitarMonitorProcessor::process(const float* input,
                                     unsigned int inputChannels,
                                     float* output,
                                     unsigned int outputChannels,
                                     std::uint32_t frameCount) noexcept
{
    if (output == nullptr || outputChannels == 0) {
        return;
    }

    const auto safeOutputChannels = std::min(outputChannels, maxChannels);
    const auto sourceChannel = inputChannels == 0
        ? 0U
        : std::min(inputChannel_.load(std::memory_order_relaxed), inputChannels - 1);

    const auto inputGain = inputGain_.load(std::memory_order_relaxed);
    const auto outputGain = outputGain_.load(std::memory_order_relaxed);
    const auto threshold = gateThreshold_.load(std::memory_order_relaxed);
    const auto drive = drive_.load(std::memory_order_relaxed);
    const auto highPassHz = highPassHz_.load(std::memory_order_relaxed);
    const auto highPassR = std::exp(-2.0F * pi * highPassHz / sampleRate_);

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        float mono = 0.0F;
        if (input != nullptr && inputChannels > 0) {
            mono = input[(frame * inputChannels) + sourceChannel] * inputGain;
        }

        if (std::fabs(mono) < threshold) {
            mono = 0.0F;
        }

        for (unsigned int channel = 0; channel < outputChannels; ++channel) {
            float value = 0.0F;
            if (channel < safeOutputChannels) {
                value = mono - previousInput_[channel] + (highPassR * previousOutput_[channel]);
                previousInput_[channel] = mono;
                previousOutput_[channel] = value;
                value = softClip(value, drive) * outputGain;
            }

            output[(frame * outputChannels) + channel] = value;
        }
    }
}

} // namespace ilys::dsp

