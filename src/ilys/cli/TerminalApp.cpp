#include "ilys/cli/TerminalApp.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace ilys::cli {
namespace {

std::vector<std::string> split(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string token;
    bool inQuotes = false;

    for (const auto character : line) {
        if (character == '"') {
            inQuotes = !inQuotes;
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(character)) && !inQuotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
            continue;
        }

        token.push_back(character);
    }

    if (inQuotes) {
        throw std::runtime_error("Unclosed quote in command.");
    }

    if (!token.empty()) {
        tokens.push_back(token);
    }

    if (!tokens.empty() && tokens[0].starts_with("./")) {
        tokens[0].erase(0, 2);
    }
    if (!tokens.empty() && tokens[0].starts_with('/')) {
        tokens[0].erase(0, 1);
    }

    return tokens;
}

char waitForSpace()
{
    std::cout << "recording... press space to stop.\n";
#if defined(_WIN32)
    while (true) {
        const auto character = static_cast<char>(_getch());
        if (character == ' ') {
            std::cout << '\n';
            return character;
        }
    }
#else
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        char character = '\0';
        while (std::cin.get(character)) {
            if (character == ' ') {
                break;
            }
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return character;
    }

    auto raw = original;
    raw.c_lflag &= static_cast<tcflag_t>(~static_cast<tcflag_t>(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    char character = '\0';
    while (read(STDIN_FILENO, &character, 1) == 1) {
        if (character == ' ') {
            break;
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    std::cout << '\n';
    return character;
#endif
}

std::string safeFileStem(const std::string& name)
{
    std::string stem;
    stem.reserve(name.size());
    for (const auto character : name) {
        if (std::isalnum(static_cast<unsigned char>(character)) || character == '-' || character == '_') {
            stem.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        } else {
            stem.push_back('_');
        }
    }

    return stem.empty() ? "region" : stem;
}

void writeLe16(std::ostream& output, unsigned int value)
{
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void writeLe32(std::ostream& output, unsigned int value)
{
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
    output.put(static_cast<char>((value >> 16U) & 0xFFU));
    output.put(static_cast<char>((value >> 24U) & 0xFFU));
}

unsigned int readLe32(const unsigned char* bytes)
{
    return static_cast<unsigned int>(bytes[0])
        | (static_cast<unsigned int>(bytes[1]) << 8U)
        | (static_cast<unsigned int>(bytes[2]) << 16U)
        | (static_cast<unsigned int>(bytes[3]) << 24U);
}

void writeFloatWav(const std::filesystem::path& path, const audio::AudioClip& clip)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary};
    if (!output) {
        throw std::runtime_error("Could not write audio file: " + path.string());
    }

    const auto dataSize = static_cast<unsigned int>(clip.samples.size() * sizeof(float));
    output.write("RIFF", 4);
    writeLe32(output, 36U + dataSize);
    output.write("WAVE", 4);
    output.write("fmt ", 4);
    writeLe32(output, 16);
    writeLe16(output, 3);
    writeLe16(output, 1);
    writeLe32(output, clip.sampleRate);
    writeLe32(output, clip.sampleRate * sizeof(float));
    writeLe16(output, sizeof(float));
    writeLe16(output, 32);
    output.write("data", 4);
    writeLe32(output, dataSize);
    output.write(reinterpret_cast<const char*>(clip.samples.data()), static_cast<std::streamsize>(dataSize));
}

audio::AudioClip readFloatWav(const std::filesystem::path& path)
{
    audio::AudioClip clip;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        return clip;
    }

    std::array<unsigned char, 44> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() != static_cast<std::streamsize>(header.size())
        || std::string_view{reinterpret_cast<const char*>(header.data()), 4} != "RIFF"
        || std::string_view{reinterpret_cast<const char*>(header.data() + 8), 4} != "WAVE"
        || std::string_view{reinterpret_cast<const char*>(header.data() + 12), 4} != "fmt "
        || std::string_view{reinterpret_cast<const char*>(header.data() + 36), 4} != "data") {
        return clip;
    }

    clip.sampleRate = readLe32(header.data() + 24);
    const auto dataSize = readLe32(header.data() + 40);
    clip.samples.resize(dataSize / sizeof(float));
    input.read(reinterpret_cast<char*>(clip.samples.data()), static_cast<std::streamsize>(dataSize));
    if (!input) {
        clip.samples.clear();
    }

    return clip;
}

std::string beatRuler(unsigned int visibleBeats, std::size_t width)
{
    std::string ruler;
    if (visibleBeats == 0 || width == 0) {
        return std::string(width, ' ');
    }

    for (unsigned int beat = 1; beat <= visibleBeats; ++beat) {
        ruler += std::to_string(beat);
        if (beat == visibleBeats) {
            break;
        }
        ruler += beat % 4 == 0 ? " | " : "  ";
    }

    if (ruler.size() > width) {
        ruler.resize(width);
    } else if (ruler.size() < width) {
        ruler.append(width - ruler.size(), ' ');
    }
    return ruler;
}

std::string regionLane(const audio::AudioClip& clip, double bpm, unsigned int visibleBeats, std::size_t width, bool muted)
{
    std::string lane(width, ' ');
    if (!clip.samples.empty() && clip.sampleRate > 0) {
        const auto beatSeconds = 60.0 / std::clamp(bpm, 20.0, 300.0);
        const auto visibleSeconds = beatSeconds * static_cast<double>(visibleBeats);
        const auto clipSeconds = static_cast<double>(clip.samples.size()) / static_cast<double>(clip.sampleRate);
        const auto filled = std::max<std::size_t>(
            1,
            std::min<std::size_t>(width, static_cast<std::size_t>(std::ceil((clipSeconds / visibleSeconds) * static_cast<double>(width))))
        );
        std::fill(lane.begin(), lane.begin() + static_cast<std::ptrdiff_t>(filled), '#');
    }

    if (muted && !lane.empty()) {
        lane[0] = 'M';
    }

    return lane;
}

void printResult(const core::Result& result)
{
    std::cout << (result.ok ? "ok: " : "error: ") << result.message << '\n';
}

} // namespace

TerminalApp::TerminalApp(std::filesystem::path presetRoot, std::filesystem::path projectRoot)
    : presets_{std::move(presetRoot)}
    , projects_{std::move(projectRoot)}
{
    midi_.setNoteHandlers(
        [this](unsigned int note, float velocity) {
            audio_.noteOn(note, velocity);
        },
        [this](unsigned int note) {
            audio_.noteOff(note);
        }
    );
}

int TerminalApp::run()
{
    printWelcome();

    std::string line;
    while (std::cout << prompt() && std::getline(std::cin, line)) {
        if (!execute(line)) {
            break;
        }
    }

    midi_.stop();
    audio_.stop();
    std::cout << "bye.\n";
    return 0;
}

void TerminalApp::printWelcome() const
{
    std::cout
        << "\n"
        << "ILYS-MT\n"
        << "terminal music workstation\n"
        << "\n"
        << "Welcome. Type '/help' to see startup commands.\n"
        << "Preset root: " << presets_.root() << '\n'
        << "Project root: " << projects_.root() << "\n\n";
}

void TerminalApp::printHelp() const
{
    if (activeProject_) {
        printProjectHelp();
    } else {
        printStartupHelp();
    }
}

void TerminalApp::printStartupHelp() const
{
    std::cout
        << "commands:\n"
        << "  /help                         show this help\n"
        << "  /create project \"name\"        create and open a project\n"
        << "  /open project \"name\"          open an existing project\n"
        << "  /projects                     list existing projects\n"
        << "  /audiovisualizer              create the audiovisualizer folder\n"
        << "  /input [index]                list or select audio input device\n"
        << "  /output [index]               list or select audio output device\n"
        << "  /midi input [index]           list or select MIDI input device\n"
        << "  /devices                      list audio and MIDI devices\n"
        << "  /quit                         exit\n";
}

void TerminalApp::printProjectHelp() const
{
    std::cout
        << "project commands:\n"
        << "  /help                         show this help\n"
        << "  /create region \"name\"         create and select a region\n"
        << "  /select region \"name\"         select a region\n"
        << "  /regions                      show project region view\n"
        << "  /bpm <value>                  set project BPM\n"
        << "  /key <name>                   set project key\n"
        << "  /metronome on|off             enable or disable recording click\n"
        << "  /countin <beats>              set recording count-in beats\n"
        << "  /record                       record into the selected region\n"
        << "  /play                         play all unmuted recorded regions\n"
        << "  /play <start> <end>           play a beat range once\n"
        << "  /loop start [start] [end]     loop beat range and monitor live input\n"
        << "  /loop stop                    stop loop playback\n"
        << "  /import \"path\"                import WAV or MP3 into selected region\n"
        << "  /delete recording             delete audio from selected region\n"
        << "  /mute region                  mute or unmute the selected region\n"
        << "  /preset region <cat> <id>     set preset for selected region\n"
        << "  /devices                      list audio and MIDI devices\n"
        << "  /input [index]                list or select audio input device\n"
        << "  /output [index]               list or select audio output device\n"
        << "  /midi input [index]           list or select MIDI input device\n"
        << "  presets                       list preset categories\n"
        << "  presets <category>            list presets for a category\n"
        << "  load <category> <id>          load an instrument preset\n"
        << "  start                         start live monitoring\n"
        << "  stop                          stop live monitoring\n"
        << "  status                        show current engine state\n"
        << "  quit                          exit\n";
}

void TerminalApp::printDevices() const
{
    const auto inputs = audio_.listInputDevices();
    const auto outputs = audio_.listOutputDevices();

    std::cout << "inputs:\n";
    if (inputs.empty()) {
        std::cout << "  none\n";
    }
    for (const auto& device : inputs) {
        std::cout << "  [" << device.index << "] " << device.name
                  << " (" << (device.channelCount == 0 ? "auto" : std::to_string(device.channelCount)) << " ch)"
                  << (device.isDefault ? " default" : "") << '\n';
    }

    std::cout << "outputs:\n";
    if (outputs.empty()) {
        std::cout << "  none\n";
    }
    for (const auto& device : outputs) {
        std::cout << "  [" << device.index << "] " << device.name
                  << " (" << (device.channelCount == 0 ? "auto" : std::to_string(device.channelCount)) << " ch)"
                  << (device.isDefault ? " default" : "") << '\n';
    }
}

void TerminalApp::printInputDevices() const
{
    const auto inputs = audio_.listInputDevices();

    std::cout << "inputs:\n";
    if (inputs.empty()) {
        std::cout << "  none\n";
    }
    for (const auto& device : inputs) {
        std::cout << "  [" << device.index << "] " << device.name
                  << " (" << (device.channelCount == 0 ? "auto" : std::to_string(device.channelCount)) << " ch)"
                  << (device.isDefault ? " default" : "") << '\n';
    }
}

void TerminalApp::printOutputDevices() const
{
    const auto outputs = audio_.listOutputDevices();

    std::cout << "outputs:\n";
    if (outputs.empty()) {
        std::cout << "  none\n";
    }
    for (const auto& device : outputs) {
        std::cout << "  [" << device.index << "] " << device.name
                  << " (" << (device.channelCount == 0 ? "auto" : std::to_string(device.channelCount)) << " ch)"
                  << (device.isDefault ? " default" : "") << '\n';
    }
}

void TerminalApp::printMidiDevices() const
{
    const auto inputs = midi_.listInputDevices();

    std::cout << "midi inputs:\n";
    if (inputs.empty()) {
        std::cout << "  none\n";
    }

    const auto selected = midi_.selectedInputIndex();
    for (const auto& device : inputs) {
        std::cout << "  [" << device.index << "] " << device.name
                  << (selected && *selected == device.index ? " selected" : "") << '\n';
    }
}

void TerminalApp::printProjects() const
{
    const auto availableProjects = projects_.listProjects();
    if (availableProjects.empty()) {
        std::cout << "no projects found in " << projects_.root() << '\n';
        return;
    }

    for (const auto& project : availableProjects) {
        std::cout << "  " << project.name << "  " << project.path << '\n';
    }
}

void TerminalApp::printRegion(const Region& region) const
{
    constexpr auto width = 64;
    constexpr auto innerWidth = width - 2;
    const auto visibleBeats = std::max(16U, loopEndBeat_);
    const auto contents = regionLane(region.clip, projectBpm_, visibleBeats, innerWidth, region.muted);

    const auto duration = region.clip.sampleRate == 0
        ? 0.0
        : static_cast<double>(region.clip.samples.size()) / static_cast<double>(region.clip.sampleRate);

    std::cout << "region: " << region.name;
    if (&region == selectedRegion()) {
        std::cout << " selected";
    }
    std::cout << " preset " << region.presetCategory << "/" << region.presetId;
    if (!region.clip.samples.empty()) {
        std::cout << " " << std::fixed << std::setprecision(2) << duration << "s";
    }
    std::cout << '\n'
              << " " << beatRuler(visibleBeats, innerWidth) << '\n'
              << std::string(width, '-') << '\n'
              << '|' << contents << "|\n"
              << std::string(width, '-') << '\n';
}

void TerminalApp::printSelectedRegion() const
{
    const auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "no region selected.\n";
        return;
    }

    printRegion(*region);
}

void TerminalApp::printProjectUi() const
{
    if (!activeProject_) {
        return;
    }

    std::cout
        << '\n'
        << "ILYS-MT Project: " << activeProject_->name << '\n'
        << "BPM: " << std::fixed << std::setprecision(1) << projectBpm_
        << "    Key: " << projectKey_
        << "    Regions: " << regions_.size()
        << "    Metronome: " << (metronomeEnabled_ ? "on" : "off")
        << "    Count-in: " << countInBeats_
        << "    Loop: " << (loopActive_ ? "on" : "off")
        << " [" << loopStartBeat_ << "-" << loopEndBeat_ << "]\n"
        << std::string(78, '=') << '\n';

    if (regions_.empty()) {
        std::cout << "  no regions. Use /create region \"name\".\n";
    }

    for (std::size_t index = 0; index < regions_.size(); ++index) {
        const auto& region = regions_[index];
        const auto selected = selectedRegionIndex_ && *selectedRegionIndex_ == index;
        const auto duration = region.clip.sampleRate == 0
            ? 0.0
            : static_cast<double>(region.clip.samples.size()) / static_cast<double>(region.clip.sampleRate);
        const auto visibleBeats = std::max(16U, loopEndBeat_);
        constexpr auto laneWidth = 62U;

        std::cout
            << (selected ? "> " : "  ")
            << '[' << (region.muted ? 'M' : ' ') << "] "
            << std::left << std::setw(20) << region.name
            << " "
            << std::right << std::setw(6) << std::fixed << std::setprecision(2) << duration << "s"
            << "  " << std::left << std::setw(18) << (region.presetCategory + "/" + region.presetId)
            << "  " << (region.clip.samples.empty() ? "empty" : region.audioFile)
            << '\n'
            << "    " << beatRuler(visibleBeats, laneWidth) << '\n'
            << "    " << regionLane(region.clip, projectBpm_, visibleBeats, laneWidth, region.muted) << '\n';
    }

    std::cout << std::string(78, '=') << '\n';
}

void TerminalApp::printStatus() const
{
    const auto& settings = audio_.settings();
    std::cout
        << "project: " << (activeProject_ ? activeProject_->name : "none") << '\n'
        << "bpm: " << projectBpm_ << '\n'
        << "key: " << projectKey_ << '\n'
        << "metronome: " << (metronomeEnabled_ ? "on" : "off") << '\n'
        << "count-in: " << countInBeats_ << " beats\n"
        << "loop range: " << loopStartBeat_ << "-" << loopEndBeat_ << '\n'
        << "state: " << (audio_.isRunning() ? "monitoring" : "stopped") << '\n'
        << "preset: " << currentPresetCategory_ << "/" << currentPresetId_ << '\n'
        << "source: " << (currentPresetUsesMidi_ ? "midi instrument" : "audio monitor") << '\n'
        << "input: " << (settings.inputDeviceIndex ? std::to_string(*settings.inputDeviceIndex) : "default") << '\n'
        << "output: " << (settings.outputDeviceIndex ? std::to_string(*settings.outputDeviceIndex) : "default") << '\n'
        << "midi input: " << (midi_.selectedInputIndex() ? std::to_string(*midi_.selectedInputIndex()) : "default") << '\n'
        << "midi: " << (midi_.isRunning() ? "running" : "stopped") << '\n'
        << "sample rate: " << settings.sampleRate << '\n'
        << "buffer: " << settings.framesPerBuffer << " frames\n"
        << "regions: " << regions_.size() << '\n'
        << "selected region: " << (selectedRegion() != nullptr ? selectedRegion()->name : "none") << '\n';
}

void TerminalApp::printCategories() const
{
    const auto categories = presets_.listCategories();
    if (categories.empty()) {
        std::cout << "no preset categories found in " << presets_.root() << '\n';
        return;
    }

    for (const auto& category : categories) {
        std::cout << "  " << category << '\n';
    }
}

void TerminalApp::printPresets(const std::string& category) const
{
    const auto instrumentPresets = presets_.listPresets(category);
    if (instrumentPresets.empty()) {
        std::cout << "no presets found in " << (presets_.root() / category) << '\n';
        return;
    }

    for (const auto& preset : instrumentPresets) {
        std::cout << "  " << std::left << std::setw(16) << preset.id
                  << preset.name << '\n';
    }
}

void TerminalApp::enterProject(project::ProjectInfo project)
{
    activeProject_ = std::move(project);
    regions_.clear();
    selectedRegionIndex_.reset();
    loopActive_ = false;
    loadProjectSettings();
    loadProjectRegions();
    saveProjectSettings();

    try {
        const auto preset = presets_.loadPreset(currentPresetCategory_, currentPresetId_);
        printResult(audio_.applyPreset(preset));
        currentPresetUsesMidi_ = preset.source == "midi";
    } catch (const std::exception& ex) {
        std::cout << "warning: " << ex.what() << '\n';
    }

    if (selectedRegion() != nullptr) {
        try {
            applySelectedRegionPreset();
        } catch (const std::exception& ex) {
            std::cout << "warning: " << ex.what() << '\n';
        }
    }

    printProjectUi();
}

void TerminalApp::loadProjectSettings()
{
    projectBpm_ = 120.0;
    projectKey_ = "C";
    metronomeEnabled_ = true;
    countInBeats_ = 4;
    loopStartBeat_ = 1;
    loopEndBeat_ = 16;
    if (!activeProject_) {
        return;
    }

    std::ifstream input{activeProject_->path / "project.json"};
    if (!input) {
        return;
    }

    const auto json = nlohmann::json::parse(input);
    projectBpm_ = json.value("bpm", projectBpm_);
    projectKey_ = json.value("key", projectKey_);
    metronomeEnabled_ = json.value("metronome", metronomeEnabled_);
    countInBeats_ = json.value("count_in_beats", countInBeats_);
    loopStartBeat_ = json.value("loop_start_beat", loopStartBeat_);
    loopEndBeat_ = json.value("loop_end_beat", loopEndBeat_);
    if (loopEndBeat_ < loopStartBeat_) {
        loopEndBeat_ = loopStartBeat_;
    }
}

void TerminalApp::saveProjectSettings() const
{
    if (!activeProject_) {
        return;
    }

    const nlohmann::json json{
        {"name", activeProject_->name},
        {"version", 1},
        {"type", "ilys-mt-project"},
        {"bpm", projectBpm_},
        {"key", projectKey_},
        {"metronome", metronomeEnabled_},
        {"count_in_beats", countInBeats_},
        {"loop_start_beat", loopStartBeat_},
        {"loop_end_beat", loopEndBeat_}
    };

    std::ofstream output{activeProject_->path / "project.json"};
    output << json.dump(2) << '\n';
}

void TerminalApp::loadProjectRegions()
{
    if (!activeProject_) {
        return;
    }

    const auto metadataPath = activeProject_->path / "regions" / "regions.json";
    std::ifstream input{metadataPath};
    if (!input) {
        return;
    }

    const auto json = nlohmann::json::parse(input);
    for (const auto& item : json) {
        Region region;
        region.name = item.value("name", "");
        region.audioFile = item.value("audio_file", "");
        region.presetCategory = item.value("preset_category", currentPresetCategory_);
        region.presetId = item.value("preset_id", currentPresetId_);
        region.muted = item.value("muted", false);
        if (region.name.empty()) {
            continue;
        }
        if (!region.audioFile.empty()) {
            region.clip = readFloatWav(activeProject_->path / "audio" / region.audioFile);
        }
        regions_.push_back(std::move(region));
    }

    if (!regions_.empty()) {
        selectedRegionIndex_ = 0;
    }
}

void TerminalApp::saveProjectRegions() const
{
    if (!activeProject_) {
        return;
    }

    const auto metadataPath = activeProject_->path / "regions" / "regions.json";
    std::filesystem::create_directories(metadataPath.parent_path());

    nlohmann::json json = nlohmann::json::array();
    for (const auto& region : regions_) {
        json.push_back({
            {"name", region.name},
            {"audio_file", region.audioFile},
            {"preset_category", region.presetCategory},
            {"preset_id", region.presetId},
            {"muted", region.muted}
        });
    }

    std::ofstream output{metadataPath};
    output << json.dump(2) << '\n';
}

void TerminalApp::saveRegionAudio(Region& region) const
{
    if (!activeProject_ || region.clip.samples.empty()) {
        return;
    }

    if (region.audioFile.empty()) {
        region.audioFile = std::to_string(selectedRegionIndex_.value_or(0) + 1) + "-" + safeFileStem(region.name) + ".wav";
    }

    writeFloatWav(activeProject_->path / "audio" / region.audioFile, region.clip);
}

TerminalApp::Region* TerminalApp::findRegion(const std::string& name)
{
    const auto found = std::find_if(regions_.begin(), regions_.end(), [&name](const Region& region) {
        return region.name == name;
    });

    return found == regions_.end() ? nullptr : &(*found);
}

const TerminalApp::Region* TerminalApp::selectedRegion() const
{
    if (!selectedRegionIndex_ || *selectedRegionIndex_ >= regions_.size()) {
        return nullptr;
    }

    return &regions_[*selectedRegionIndex_];
}

TerminalApp::Region* TerminalApp::selectedRegion()
{
    if (!selectedRegionIndex_ || *selectedRegionIndex_ >= regions_.size()) {
        return nullptr;
    }

    return &regions_[*selectedRegionIndex_];
}

void TerminalApp::createRegion(const std::string& name)
{
    if (name.empty()) {
        std::cout << "error: Region name cannot be empty.\n";
        return;
    }

    if (findRegion(name) != nullptr) {
        std::cout << "error: Region already exists: " << name << '\n';
        return;
    }

    Region region;
    region.name = name;
    region.presetCategory = currentPresetCategory_;
    region.presetId = currentPresetId_;
    regions_.push_back(std::move(region));
    selectedRegionIndex_ = regions_.size() - 1;
    saveProjectRegions();
    applySelectedRegionPreset();
    printProjectUi();
    printSelectedRegion();
}

void TerminalApp::selectRegion(const std::string& name)
{
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        if (regions_[index].name == name) {
            selectedRegionIndex_ = index;
            applySelectedRegionPreset();
            printProjectUi();
            printSelectedRegion();
            return;
        }
    }

    std::cout << "error: Region not found: " << name << '\n';
}

void TerminalApp::muteSelectedRegion()
{
    auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "error: Create or select a region first.\n";
        return;
    }

    region->muted = !region->muted;
    saveProjectRegions();
    std::cout << "ok: Region " << (region->muted ? "muted: " : "unmuted: ") << region->name << '\n';
    printProjectUi();
    printRegion(*region);
}

void TerminalApp::recordSelectedRegion()
{
    auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "error: Create or select a region first.\n";
        return;
    }

    if (!region->clip.samples.empty()) {
        std::cout << "warning: Region already contains audio. Type 'yes' to overwrite: ";
        std::string confirmation;
        std::getline(std::cin, confirmation);
        if (confirmation != "yes") {
            std::cout << "ok: Recording cancelled.\n";
            return;
        }
    }

    applySelectedRegionPreset();

    const auto result = audio_.beginRecording(projectBpm_, metronomeEnabled_, countInBeats_);
    printResult(result);
    if (!result.ok) {
        return;
    }

    waitForSpace();
    region->clip = audio_.finishRecording();
    saveRegionAudio(*region);
    saveProjectRegions();
    std::cout << "ok: Recorded " << region->clip.samples.size() << " samples into " << region->name << ".\n";
    printProjectUi();
    printRegion(*region);
}

void TerminalApp::playRegions(bool loop,
                              bool monitorInput,
                              std::optional<std::pair<unsigned int, unsigned int>> beatRange)
{
    std::vector<audio::AudioClip> clips;
    if (beatRange && (beatRange->first == 0 || beatRange->second < beatRange->first)) {
        std::cout << "error: Beat ranges start at 1 and the end beat must be greater than or equal to the start beat.\n";
        return;
    }

    for (const auto& region : regions_) {
        if (!region.muted && !region.clip.samples.empty()) {
            if (beatRange) {
                auto clipped = clipForBeatRange(region.clip, beatRange->first, beatRange->second);
                if (!clipped.samples.empty()) {
                    clips.push_back(std::move(clipped));
                }
            } else {
                clips.push_back(region.clip);
            }
        }
    }

    if (loop && beatRange) {
        loopStartBeat_ = beatRange->first;
        loopEndBeat_ = beatRange->second;
        saveProjectSettings();
    }

    const auto result = audio_.playClips(std::move(clips), loop, monitorInput);
    loopActive_ = result.ok && loop;
    printResult(result);
    if (result.ok) {
        printProjectUi();
    }
}

void TerminalApp::stopLoop()
{
    audio_.stopPlayback();
    loopActive_ = false;
    std::cout << "ok: Loop playback stopped.\n";
    printProjectUi();
}

void TerminalApp::importIntoSelectedRegion(const std::filesystem::path& path)
{
    auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "error: Create or select a region first.\n";
        return;
    }

    if (!region->clip.samples.empty()) {
        std::cout << "warning: Region already contains audio. Type 'yes' to replace it with the import: ";
        std::string confirmation;
        std::getline(std::cin, confirmation);
        if (confirmation != "yes") {
            std::cout << "ok: Import cancelled.\n";
            return;
        }
    }

    audio::AudioClip clip;
    const auto result = audio_.loadClipFromFile(path, clip);
    printResult(result);
    if (!result.ok) {
        return;
    }

    region->clip = std::move(clip);
    region->audioFile = safeFileStem(region->name) + "-import.wav";
    saveRegionAudio(*region);
    saveProjectRegions();
    printProjectUi();
    printRegion(*region);
}

void TerminalApp::deleteSelectedRecording()
{
    auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "error: Create or select a region first.\n";
        return;
    }

    if (region->clip.samples.empty()) {
        std::cout << "ok: Selected region is already empty.\n";
        return;
    }

    if (activeProject_ && !region->audioFile.empty()) {
        std::filesystem::remove(activeProject_->path / "audio" / region->audioFile);
    }
    region->clip.samples.clear();
    region->audioFile.clear();
    saveProjectRegions();
    std::cout << "ok: Deleted recording from " << region->name << ".\n";
    printProjectUi();
    printRegion(*region);
}

void TerminalApp::setProjectBpm(const std::string& value)
{
    const auto bpm = std::stod(value);
    if (bpm < 20.0 || bpm > 300.0) {
        std::cout << "error: BPM must be between 20 and 300.\n";
        return;
    }

    projectBpm_ = bpm;
    saveProjectSettings();
    printProjectUi();
}

void TerminalApp::setProjectKey(std::string key)
{
    if (key.empty()) {
        std::cout << "error: Key cannot be empty.\n";
        return;
    }

    projectKey_ = std::move(key);
    saveProjectSettings();
    printProjectUi();
}

void TerminalApp::setMetronome(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    if (value == "on") {
        metronomeEnabled_ = true;
    } else if (value == "off") {
        metronomeEnabled_ = false;
    } else {
        std::cout << "error: Use /metronome on or /metronome off.\n";
        return;
    }

    saveProjectSettings();
    printProjectUi();
}

void TerminalApp::setCountIn(const std::string& value)
{
    const auto beats = static_cast<unsigned int>(std::stoul(value));
    if (beats > 16) {
        std::cout << "error: Count-in must be between 0 and 16 beats.\n";
        return;
    }

    countInBeats_ = beats;
    saveProjectSettings();
    printProjectUi();
}

void TerminalApp::setSelectedRegionPreset(const std::string& category, const std::string& id)
{
    auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "error: Create or select a region first.\n";
        return;
    }

    try {
        const auto preset = presets_.loadPreset(category, id);
        printResult(audio_.applyPreset(preset));
        currentPresetCategory_ = preset.category;
        currentPresetId_ = preset.id;
        currentPresetUsesMidi_ = preset.source == "midi";
        region->presetCategory = preset.category;
        region->presetId = preset.id;
        saveProjectRegions();
        printProjectUi();
    } catch (const std::exception& ex) {
        std::cout << "error: " << ex.what() << '\n';
    }
}

void TerminalApp::applySelectedRegionPreset()
{
    const auto* region = selectedRegion();
    if (region == nullptr) {
        return;
    }

    const auto preset = presets_.loadPreset(region->presetCategory, region->presetId);
    printResult(audio_.applyPreset(preset));
    currentPresetCategory_ = preset.category;
    currentPresetId_ = preset.id;
    currentPresetUsesMidi_ = preset.source == "midi";
}

std::size_t TerminalApp::samplesPerBeat(unsigned int sampleRate) const
{
    return std::max<std::size_t>(
        1,
        static_cast<std::size_t>(
            std::round((60.0 / std::clamp(projectBpm_, 20.0, 300.0)) * static_cast<double>(sampleRate))
        )
    );
}

audio::AudioClip TerminalApp::clipForBeatRange(const audio::AudioClip& clip,
                                               unsigned int startBeat,
                                               unsigned int endBeat) const
{
    audio::AudioClip result;
    result.sampleRate = clip.sampleRate;
    if (clip.samples.empty() || clip.sampleRate == 0 || startBeat == 0 || endBeat < startBeat) {
        return result;
    }

    const auto beatSamples = samplesPerBeat(clip.sampleRate);
    const auto startSample = static_cast<std::size_t>(startBeat - 1) * beatSamples;
    const auto endSampleExclusive = static_cast<std::size_t>(endBeat) * beatSamples;
    const auto loopSamples = endSampleExclusive - startSample;
    result.samples.assign(loopSamples, 0.0F);

    if (startSample >= clip.samples.size()) {
        return result;
    }

    const auto sourceEnd = std::min<std::size_t>(clip.samples.size(), endSampleExclusive);
    std::copy(
        clip.samples.begin() + static_cast<std::ptrdiff_t>(startSample),
        clip.samples.begin() + static_cast<std::ptrdiff_t>(sourceEnd),
        result.samples.begin()
    );

    return result;
}

bool TerminalApp::executeStartupCommand(const std::vector<std::string>& tokens)
{
    const auto& command = tokens[0];

    if (command == "help") {
        printStartupHelp();
    } else if (command == "create" && tokens.size() == 3 && tokens[1] == "project") {
        project::ProjectInfo project;
        const auto result = projects_.createProject(tokens[2], project);
        printResult(result);
        if (result.ok) {
            enterProject(std::move(project));
        }
    } else if (command == "open" && tokens.size() == 3 && tokens[1] == "project") {
        project::ProjectInfo project;
        const auto result = projects_.openProject(tokens[2], project);
        printResult(result);
        if (result.ok) {
            enterProject(std::move(project));
        }
    } else if (command == "projects") {
        printProjects();
    } else if (command == "audiovisualizer") {
        std::filesystem::path path;
        printResult(projects_.ensureAudiovisualizerFolder(path));
    } else if (command == "devices") {
        printDevices();
        printMidiDevices();
    } else if (command == "input" && tokens.size() == 1) {
        printInputDevices();
    } else if (command == "input" && tokens.size() == 2) {
        printResult(audio_.setInputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
    } else if (command == "output" && tokens.size() == 1) {
        printOutputDevices();
    } else if (command == "output" && tokens.size() == 2) {
        printResult(audio_.setOutputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
    } else if (command == "midi" && tokens.size() == 2 && tokens[1] == "input") {
        printMidiDevices();
    } else if (command == "midi" && tokens.size() == 3 && tokens[1] == "input") {
        printResult(midi_.setInputDevice(static_cast<unsigned int>(std::stoul(tokens[2]))));
    } else if (command == "quit" || command == "exit") {
        return false;
    } else {
        std::cout << "unknown startup command. Type '/help'.\n";
    }

    return true;
}

bool TerminalApp::executeProjectCommand(const std::vector<std::string>& tokens)
{
    const auto& command = tokens[0];

    if (command == "help") {
        printProjectHelp();
    } else if (command == "create" && tokens.size() == 3 && tokens[1] == "region") {
        createRegion(tokens[2]);
    } else if (command == "select" && tokens.size() == 3 && tokens[1] == "region") {
        selectRegion(tokens[2]);
    } else if (command == "regions" && tokens.size() == 1) {
        printProjectUi();
    } else if (command == "bpm" && tokens.size() == 2) {
        setProjectBpm(tokens[1]);
    } else if (command == "key" && tokens.size() == 2) {
        setProjectKey(tokens[1]);
    } else if (command == "metronome" && tokens.size() == 2) {
        setMetronome(tokens[1]);
    } else if ((command == "countin" || command == "count-in") && tokens.size() == 2) {
        setCountIn(tokens[1]);
    } else if (command == "record" && tokens.size() == 1) {
        recordSelectedRegion();
    } else if (command == "play" && tokens.size() == 1) {
        playRegions(false, false, std::nullopt);
    } else if (command == "play" && tokens.size() == 3) {
        playRegions(false, false, std::make_pair(
            static_cast<unsigned int>(std::stoul(tokens[1])),
            static_cast<unsigned int>(std::stoul(tokens[2]))
        ));
    } else if (command == "loop" && tokens.size() == 2 && tokens[1] == "start") {
        playRegions(true, true, std::make_pair(loopStartBeat_, loopEndBeat_));
    } else if (command == "loop" && tokens.size() == 4 && tokens[1] == "start") {
        playRegions(true, true, std::make_pair(
            static_cast<unsigned int>(std::stoul(tokens[2])),
            static_cast<unsigned int>(std::stoul(tokens[3]))
        ));
    } else if (command == "loop" && tokens.size() == 2 && tokens[1] == "stop") {
        stopLoop();
    } else if (command == "import" && tokens.size() == 2) {
        importIntoSelectedRegion(tokens[1]);
    } else if (command == "delete" && tokens.size() == 2 && tokens[1] == "recording") {
        deleteSelectedRecording();
    } else if (command == "mute" && tokens.size() == 2 && tokens[1] == "region") {
        muteSelectedRegion();
    } else if (command == "preset" && tokens.size() == 4 && tokens[1] == "region") {
        setSelectedRegionPreset(tokens[2], tokens[3]);
    } else if (command == "devices") {
        printDevices();
        printMidiDevices();
    } else if (command == "input" && tokens.size() == 1) {
        printInputDevices();
    } else if (command == "input" && tokens.size() == 2) {
        printResult(audio_.setInputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
    } else if (command == "output" && tokens.size() == 1) {
        printOutputDevices();
    } else if (command == "output" && tokens.size() == 2) {
        printResult(audio_.setOutputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
    } else if (command == "midi" && tokens.size() == 2 && tokens[1] == "input") {
        printMidiDevices();
    } else if (command == "midi" && tokens.size() == 3 && tokens[1] == "input") {
        printResult(midi_.setInputDevice(static_cast<unsigned int>(std::stoul(tokens[2]))));
    } else if (command == "presets" && tokens.size() == 1) {
        printCategories();
    } else if (command == "presets" && tokens.size() == 2) {
        printPresets(tokens[1]);
    } else if (command == "load" && tokens.size() == 3) {
        const auto preset = presets_.loadPreset(tokens[1], tokens[2]);
        printResult(audio_.applyPreset(preset));
        currentPresetCategory_ = preset.category;
        currentPresetId_ = preset.id;
        currentPresetUsesMidi_ = preset.source == "midi";
        if (auto* region = selectedRegion()) {
            region->presetCategory = preset.category;
            region->presetId = preset.id;
            saveProjectRegions();
            printProjectUi();
        }
    } else if (command == "start") {
        applySelectedRegionPreset();
        const auto audioResult = audio_.start();
        printResult(audioResult);
        if (audioResult.ok && currentPresetUsesMidi_) {
            const auto midiResult = midi_.start();
            printResult(midiResult);
            if (!midiResult.ok) {
                audio_.stop();
            }
        }
    } else if (command == "stop") {
        midi_.stop();
        audio_.stop();
        loopActive_ = false;
        std::cout << "ok: monitoring stopped.\n";
    } else if (command == "status") {
        printStatus();
    } else if (command == "quit" || command == "exit") {
        return false;
    } else {
        std::cout << "unknown project command. Type '/help'.\n";
    }

    return true;
}

bool TerminalApp::execute(const std::string& line)
{
    try {
        const auto tokens = split(line);
        if (tokens.empty()) {
            return true;
        }

        if (activeProject_) {
            return executeProjectCommand(tokens);
        } else {
            return executeStartupCommand(tokens);
        }
    } catch (const std::exception& ex) {
        std::cout << "error: " << ex.what() << '\n';
    }

    return true;
}

std::string TerminalApp::prompt() const
{
    if (activeProject_) {
        return "ilys-mt[" + activeProject_->name + "]> ";
    }

    return "ilys-mt> ";
}

} // namespace ilys::cli
