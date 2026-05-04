#include "ilys/dsp/InstrumentMonitorProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <string>

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

InstrumentMonitorProcessor::Waveform parseWaveform(const std::string& waveform) noexcept
{
    if (waveform == "triangle") {
        return InstrumentMonitorProcessor::Waveform::Triangle;
    }

    if (waveform == "saw") {
        return InstrumentMonitorProcessor::Waveform::Saw;
    }

    if (waveform == "square") {
        return InstrumentMonitorProcessor::Waveform::Square;
    }

    return InstrumentMonitorProcessor::Waveform::Sine;
}

float midiNoteToFrequency(unsigned int note) noexcept
{
    return 440.0F * std::pow(2.0F, (static_cast<float>(note) - 69.0F) / 12.0F);
}

float oscillatorSample(float phase, InstrumentMonitorProcessor::Waveform waveform) noexcept
{
    const auto wrapped = phase - std::floor(phase);
    switch (waveform) {
    case InstrumentMonitorProcessor::Waveform::Triangle:
        return 4.0F * std::fabs(wrapped - 0.5F) - 1.0F;
    case InstrumentMonitorProcessor::Waveform::Saw:
        return (2.0F * wrapped) - 1.0F;
    case InstrumentMonitorProcessor::Waveform::Square:
        return wrapped < 0.5F ? 1.0F : -1.0F;
    case InstrumentMonitorProcessor::Waveform::Sine:
    default:
        return std::sin(2.0F * pi * wrapped);
    }
}

} // namespace

void InstrumentMonitorProcessor::prepare(float sampleRate, unsigned int outputChannels)
{
    sampleRate_ = sampleRate > 0.0F ? sampleRate : 48000.0F;
    preparedChannels_ = std::clamp(outputChannels, 1U, maxChannels);
    tremoloPhase_ = 0.0F;
    previousHighPassInput_.fill(0.0F);
    previousHighPassOutput_.fill(0.0F);
    previousLowPassOutput_.fill(0.0F);
    for (auto& voice : voices_) {
        voice.gate.store(false, std::memory_order_relaxed);
        voice.velocity.store(0.0F, std::memory_order_relaxed);
        voice.phase = 0.0F;
        voice.detunePhase = 0.0F;
        voice.envelope = 0.0F;
        voice.releaseStart = 0.0F;
        voice.stage = EnvelopeStage::Idle;
    }
}

void InstrumentMonitorProcessor::applyPreset(const presets::InstrumentPreset& preset)
{
    sourceMode_.store(
        preset.source == "midi" ? static_cast<int>(SourceMode::Midi) : static_cast<int>(SourceMode::Audio),
        std::memory_order_relaxed
    );
    inputChannel_.store(preset.inputChannel, std::memory_order_relaxed);
    inputGain_.store(dbToLinear(preset.inputGainDb), std::memory_order_relaxed);
    outputGain_.store(dbToLinear(preset.outputGainDb), std::memory_order_relaxed);
    highPassHz_.store(std::max(10.0F, preset.highPassHz), std::memory_order_relaxed);
    lowPassHz_.store(std::max(100.0F, preset.lowPassHz), std::memory_order_relaxed);
    gateThreshold_.store(dbToLinear(preset.gateThresholdDb), std::memory_order_relaxed);
    drive_.store(std::max(1.0F, preset.drive), std::memory_order_relaxed);
    waveform_.store(static_cast<int>(parseWaveform(preset.waveform)), std::memory_order_relaxed);
    activeVoiceLimit_.store(std::clamp(preset.maxVoices, 1U, maxVoices), std::memory_order_relaxed);
    attackMs_.store(std::max(0.1F, preset.attackMs), std::memory_order_relaxed);
    decayMs_.store(std::max(1.0F, preset.decayMs), std::memory_order_relaxed);
    sustain_.store(std::clamp(preset.sustain, 0.0F, 1.0F), std::memory_order_relaxed);
    releaseMs_.store(std::max(1.0F, preset.releaseMs), std::memory_order_relaxed);
    tone_.store(std::clamp(preset.tone, 0.0F, 1.0F), std::memory_order_relaxed);
    detuneCents_.store(preset.detuneCents, std::memory_order_relaxed);
    stereoWidth_.store(std::clamp(preset.stereoWidth, 0.0F, 1.0F), std::memory_order_relaxed);
    tremoloDepth_.store(std::clamp(preset.tremoloDepth, 0.0F, 1.0F), std::memory_order_relaxed);
    tremoloRateHz_.store(std::max(0.1F, preset.tremoloRateHz), std::memory_order_relaxed);
}

bool InstrumentMonitorProcessor::requiresAudioInput() const noexcept
{
    return sourceMode_.load(std::memory_order_relaxed) == static_cast<int>(SourceMode::Audio);
}

void InstrumentMonitorProcessor::noteOn(unsigned int note, float velocity) noexcept
{
    const auto voiceLimit = activeVoiceLimit_.load(std::memory_order_relaxed);
    for (unsigned int index = 0; index < voiceLimit; ++index) {
        if (!voices_[index].gate.load(std::memory_order_relaxed)) {
            voices_[index].note.store(note, std::memory_order_relaxed);
            voices_[index].velocity.store(std::clamp(velocity, 0.0F, 1.0F), std::memory_order_relaxed);
            voices_[index].gate.store(true, std::memory_order_relaxed);
            return;
        }
    }

    const auto stolen = nextVoiceToSteal_.fetch_add(1, std::memory_order_relaxed) % voiceLimit;
    voices_[stolen].note.store(note, std::memory_order_relaxed);
    voices_[stolen].velocity.store(std::clamp(velocity, 0.0F, 1.0F), std::memory_order_relaxed);
    voices_[stolen].gate.store(true, std::memory_order_relaxed);
}

void InstrumentMonitorProcessor::noteOff(unsigned int note) noexcept
{
    const auto voiceLimit = activeVoiceLimit_.load(std::memory_order_relaxed);
    for (unsigned int index = 0; index < voiceLimit; ++index) {
        if (voices_[index].note.load(std::memory_order_relaxed) == note) {
            voices_[index].gate.store(false, std::memory_order_relaxed);
        }
    }
}

void InstrumentMonitorProcessor::process(const float* input,
                                         unsigned int inputChannels,
                                         float* output,
                                         unsigned int outputChannels,
                                         std::uint32_t frameCount) noexcept
{
    if (output == nullptr || outputChannels == 0) {
        return;
    }

    const auto safeOutputChannels = std::min(outputChannels, maxChannels);
    const auto sourceMode = static_cast<SourceMode>(sourceMode_.load(std::memory_order_relaxed));
    if (sourceMode == SourceMode::Midi) {
        const auto outputGain = outputGain_.load(std::memory_order_relaxed);
        const auto drive = drive_.load(std::memory_order_relaxed);
        const auto lowPassHz = std::min(lowPassHz_.load(std::memory_order_relaxed), sampleRate_ * 0.45F);
        const auto tone = tone_.load(std::memory_order_relaxed);
        const auto lowPassA = 1.0F - std::exp(-2.0F * pi * lowPassHz * (0.35F + (tone * 0.65F)) / sampleRate_);
        const auto waveform = static_cast<Waveform>(waveform_.load(std::memory_order_relaxed));
        const auto voiceLimit = activeVoiceLimit_.load(std::memory_order_relaxed);
        const auto attackStep = 1.0F / ((attackMs_.load(std::memory_order_relaxed) / 1000.0F) * sampleRate_);
        const auto decayStep = 1.0F / ((decayMs_.load(std::memory_order_relaxed) / 1000.0F) * sampleRate_);
        const auto sustain = sustain_.load(std::memory_order_relaxed);
        const auto releaseStep = 1.0F / ((releaseMs_.load(std::memory_order_relaxed) / 1000.0F) * sampleRate_);
        const auto detuneRatio = std::pow(2.0F, detuneCents_.load(std::memory_order_relaxed) / 1200.0F);
        const auto stereoWidth = stereoWidth_.load(std::memory_order_relaxed);
        const auto tremoloDepth = tremoloDepth_.load(std::memory_order_relaxed);
        const auto tremoloRateHz = tremoloRateHz_.load(std::memory_order_relaxed);

        for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
            std::array<float, maxChannels> frameOut{};
            const auto tremolo = 1.0F - (tremoloDepth * 0.5F)
                + (std::sin(2.0F * pi * tremoloPhase_) * tremoloDepth * 0.5F);
            tremoloPhase_ += tremoloRateHz / sampleRate_;
            if (tremoloPhase_ >= 1.0F) {
                tremoloPhase_ -= 1.0F;
            }

            for (unsigned int voiceIndex = 0; voiceIndex < voiceLimit; ++voiceIndex) {
                auto& voice = voices_[voiceIndex];
                const auto gate = voice.gate.load(std::memory_order_relaxed);
                const auto note = voice.note.load(std::memory_order_relaxed);

                if (gate && (voice.stage == EnvelopeStage::Idle || voice.activeNote != note)) {
                    voice.activeNote = note;
                    voice.phase = 0.0F;
                    voice.detunePhase = 0.0F;
                    voice.envelope = 0.0F;
                    voice.releaseStart = 0.0F;
                    voice.stage = EnvelopeStage::Attack;
                } else if (!gate
                           && voice.stage != EnvelopeStage::Idle
                           && voice.stage != EnvelopeStage::Release) {
                    voice.releaseStart = voice.envelope;
                    voice.stage = EnvelopeStage::Release;
                }

                switch (voice.stage) {
                case EnvelopeStage::Idle:
                    continue;
                case EnvelopeStage::Attack:
                    voice.envelope += attackStep;
                    if (voice.envelope >= 1.0F) {
                        voice.envelope = 1.0F;
                        voice.stage = EnvelopeStage::Decay;
                    }
                    break;
                case EnvelopeStage::Decay:
                    voice.envelope -= decayStep * (1.0F - sustain);
                    if (voice.envelope <= sustain) {
                        voice.envelope = sustain;
                        voice.stage = EnvelopeStage::Sustain;
                    }
                    break;
                case EnvelopeStage::Sustain:
                    voice.envelope = sustain;
                    break;
                case EnvelopeStage::Release:
                    voice.envelope -= releaseStep * std::max(voice.releaseStart, 0.001F);
                    if (voice.envelope <= 0.0F) {
                        voice.envelope = 0.0F;
                        voice.stage = EnvelopeStage::Idle;
                        continue;
                    }
                    break;
                }

                const auto frequency = midiNoteToFrequency(voice.activeNote);
                voice.phase += frequency / sampleRate_;
                voice.detunePhase += (frequency * detuneRatio) / sampleRate_;
                if (voice.phase >= 1.0F) {
                    voice.phase -= std::floor(voice.phase);
                }
                if (voice.detunePhase >= 1.0F) {
                    voice.detunePhase -= std::floor(voice.detunePhase);
                }

                const auto baseSample = oscillatorSample(voice.phase, waveform);
                const auto detunedSample = oscillatorSample(voice.detunePhase, waveform);
                const auto sample = ((baseSample * (1.0F - stereoWidth)) + (detunedSample * stereoWidth))
                    * voice.envelope
                    * voice.velocity.load(std::memory_order_relaxed)
                    * tremolo;

                if (safeOutputChannels == 1) {
                    frameOut[0] += sample;
                } else {
                    frameOut[0] += (baseSample * (1.0F - stereoWidth) + detunedSample * stereoWidth)
                        * voice.envelope
                        * voice.velocity.load(std::memory_order_relaxed)
                        * tremolo;
                    frameOut[1] += (detunedSample * (1.0F - stereoWidth) + baseSample * stereoWidth)
                        * voice.envelope
                        * voice.velocity.load(std::memory_order_relaxed)
                        * tremolo;
                }
            }

            for (unsigned int channel = 0; channel < outputChannels; ++channel) {
                float value = 0.0F;
                if (channel < safeOutputChannels) {
                    previousLowPassOutput_[channel] += lowPassA * (frameOut[channel] - previousLowPassOutput_[channel]);
                    value = softClip(previousLowPassOutput_[channel], drive) * outputGain;
                }
                output[(frame * outputChannels) + channel] = value;
            }
        }

        return;
    }

    const auto sourceChannel = inputChannels == 0
        ? 0U
        : std::min(inputChannel_.load(std::memory_order_relaxed), inputChannels - 1);

    const auto inputGain = inputGain_.load(std::memory_order_relaxed);
    const auto outputGain = outputGain_.load(std::memory_order_relaxed);
    const auto threshold = gateThreshold_.load(std::memory_order_relaxed);
    const auto drive = drive_.load(std::memory_order_relaxed);
    const auto highPassHz = highPassHz_.load(std::memory_order_relaxed);
    const auto lowPassHz = std::min(lowPassHz_.load(std::memory_order_relaxed), sampleRate_ * 0.45F);
    const auto tone = tone_.load(std::memory_order_relaxed);
    const auto tremoloDepth = tremoloDepth_.load(std::memory_order_relaxed);
    const auto tremoloRateHz = tremoloRateHz_.load(std::memory_order_relaxed);
    const auto highPassR = std::exp(-2.0F * pi * highPassHz / sampleRate_);
    const auto lowPassA = 1.0F - std::exp(-2.0F * pi * lowPassHz * (0.35F + (tone * 0.65F)) / sampleRate_);

    for (std::uint32_t frame = 0; frame < frameCount; ++frame) {
        const auto tremolo = 1.0F - (tremoloDepth * 0.5F)
            + (std::sin(2.0F * pi * tremoloPhase_) * tremoloDepth * 0.5F);
        tremoloPhase_ += tremoloRateHz / sampleRate_;
        if (tremoloPhase_ >= 1.0F) {
            tremoloPhase_ -= 1.0F;
        }

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
                const auto highPassed = mono
                    - previousHighPassInput_[channel]
                    + (highPassR * previousHighPassOutput_[channel]);

                previousHighPassInput_[channel] = mono;
                previousHighPassOutput_[channel] = highPassed;
                previousLowPassOutput_[channel] += lowPassA * (highPassed - previousLowPassOutput_[channel]);

                value = softClip(previousLowPassOutput_[channel], drive) * outputGain * tremolo;
            }

            output[(frame * outputChannels) + channel] = value;
        }
    }
}

} // namespace ilys::dsp
