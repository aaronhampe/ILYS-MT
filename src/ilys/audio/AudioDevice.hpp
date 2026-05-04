#pragma once

#include <string>

namespace ilys::audio {

enum class DeviceDirection {
    Input,
    Output
};

struct AudioDevice {
    unsigned int index{0};
    std::string name{};
    DeviceDirection direction{DeviceDirection::Input};
    unsigned int channelCount{0};
    bool isDefault{false};
};

} // namespace ilys::audio

