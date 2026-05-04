#pragma once

#include "ilys/audio/AudioEngine.hpp"
#include "ilys/midi/MidiEngine.hpp"
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
    void printMidiDevices() const;
    void printStatus() const;
    void printCategories() const;
    void printPresets(const std::string& category) const;
    bool execute(const std::string& line);

    audio::AudioEngine audio_;
    midi::MidiEngine midi_;
    presets::PresetManager presets_;
    std::string currentPresetCategory_{"guitar"};
    std::string currentPresetId_{"clean_di"};
    bool currentPresetUsesMidi_{false};
};

} // namespace ilys::cli
