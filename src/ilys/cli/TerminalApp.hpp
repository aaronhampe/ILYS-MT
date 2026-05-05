#pragma once

#include "ilys/audio/AudioEngine.hpp"
#include "ilys/midi/MidiEngine.hpp"
#include "ilys/presets/PresetManager.hpp"
#include "ilys/project/ProjectManager.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ilys::cli {

class TerminalApp {
public:
    TerminalApp(std::filesystem::path presetRoot, std::filesystem::path projectRoot);

    int run();

private:
    struct Region {
        std::string name;
        audio::AudioClip clip;
        std::string audioFile;
        std::string presetCategory{"guitar"};
        std::string presetId{"clean_di"};
        bool muted{false};
    };

    void printWelcome() const;
    void printHelp() const;
    void printStartupHelp() const;
    void printProjectHelp() const;
    void printDevices() const;
    void printInputDevices() const;
    void printOutputDevices() const;
    void printMidiDevices() const;
    void printProjects() const;
    void printRegion(const Region& region) const;
    void printSelectedRegion() const;
    void printProjectUi() const;
    void printStatus() const;
    void printCategories() const;
    void printPresets(const std::string& category) const;
    void enterProject(project::ProjectInfo project);
    void loadProjectSettings();
    void saveProjectSettings() const;
    void loadProjectRegions();
    void saveProjectRegions() const;
    void saveRegionAudio(Region& region) const;
    [[nodiscard]] Region* findRegion(const std::string& name);
    [[nodiscard]] const Region* selectedRegion() const;
    [[nodiscard]] Region* selectedRegion();
    void createRegion(const std::string& name);
    void selectRegion(const std::string& name);
    void muteSelectedRegion();
    void recordSelectedRegion();
    void playRegions(bool loop, bool monitorInput, std::optional<std::pair<unsigned int, unsigned int>> beatRange);
    void stopLoop();
    void importIntoSelectedRegion(const std::filesystem::path& path);
    void deleteSelectedRecording();
    void setProjectBpm(const std::string& value);
    void setProjectKey(std::string key);
    void setMetronome(std::string value);
    void setCountIn(const std::string& value);
    void setSelectedRegionPreset(const std::string& category, const std::string& id);
    void applySelectedRegionPreset();
    [[nodiscard]] std::size_t samplesPerBeat(unsigned int sampleRate) const;
    [[nodiscard]] audio::AudioClip clipForBeatRange(const audio::AudioClip& clip,
                                                    unsigned int startBeat,
                                                    unsigned int endBeat) const;
    bool executeStartupCommand(const std::vector<std::string>& tokens);
    bool executeProjectCommand(const std::vector<std::string>& tokens);
    bool execute(const std::string& line);
    [[nodiscard]] std::string prompt() const;

    audio::AudioEngine audio_;
    midi::MidiEngine midi_;
    presets::PresetManager presets_;
    project::ProjectManager projects_;
    std::optional<project::ProjectInfo> activeProject_;
    std::vector<Region> regions_;
    std::optional<std::size_t> selectedRegionIndex_;
    double projectBpm_{120.0};
    std::string projectKey_{"C"};
    bool metronomeEnabled_{true};
    unsigned int countInBeats_{4};
    unsigned int loopStartBeat_{1};
    unsigned int loopEndBeat_{16};
    bool loopActive_{false};
    std::string currentPresetCategory_{"guitar"};
    std::string currentPresetId_{"clean_di"};
    bool currentPresetUsesMidi_{false};
};

} // namespace ilys::cli
