#pragma once

#include <string>

namespace ilys::midi {

struct MidiDevice {
    unsigned int index{0};
    std::string name{};
};

} // namespace ilys::midi

