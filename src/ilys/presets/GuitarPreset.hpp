#pragma once

#include <string>

namespace ilys::presets {

struct GuitarPreset {
    std::string id{"clean_di"};
    std::string name{"Clean DI Monitor"};
    std::string category{"guitar"};
    std::string description{};
    unsigned int inputChannel{0};
    float inputGainDb{0.0F};
    float outputGainDb{-6.0F};
    float highPassHz{80.0F};
    float gateThresholdDb{-70.0F};
    float drive{1.0F};
};

} // namespace ilys::presets

