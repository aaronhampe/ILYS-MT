#pragma once

#include "ilys/audio/AudioEngine.hpp"
#include "ilys/presets/PresetManager.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ilys::cli {

class TerminalApp {
public:
    explicit TerminalApp(std::filesystem::path presetRoot);

    int run();

private:
    void printWelcome() const;
    void printHelp() const;
    void printDevices() const;
    void printStatus() const;
    void printGuitarPresets() const;
    bool execute(const std::string& line);

    audio::AudioEngine audio_;
    presets::PresetManager presets_;
    std::string currentPresetId_{"clean_di"};
};

} // namespace ilys::cli

