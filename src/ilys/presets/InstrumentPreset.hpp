#pragma once

#include <string>

namespace ilys::presets {

struct InstrumentPreset {
    std::string id{"clean_di"};
    std::string name{"Clean DI Monitor"};
    std::string category{"guitar"};
    std::string source{"audio"};
    std::string description{};
    unsigned int inputChannel{0};
    float inputGainDb{0.0F};
    float outputGainDb{-6.0F};
    float highPassHz{80.0F};
    float lowPassHz{18000.0F};
    float gateThresholdDb{-70.0F};
    float drive{1.0F};
    std::string waveform{"sine"};
    unsigned int maxVoices{12};
    float attackMs{5.0F};
    float decayMs{80.0F};
    float sustain{0.75F};
    float releaseMs{180.0F};
    float tone{0.5F};
    float detuneCents{0.0F};
    float stereoWidth{0.35F};
    float tremoloDepth{0.0F};
    float tremoloRateHz{5.0F};
};

} // namespace ilys::presets
