#include "ilys/cli/TerminalApp.hpp"

#include <array>
#include <cctype>
#include <cstddef>
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
        << "  /record                       record into the selected region\n"
        << "  /play                         play all unmuted recorded regions\n"
        << "  /mute region                  mute or unmute the selected region\n"
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
    std::string contents(innerWidth, ' ');

    if (!region.clip.samples.empty()) {
        const auto barWidth = std::min<std::size_t>(innerWidth, region.clip.samples.size() / 2048 + 1);
        std::fill(contents.begin(), contents.begin() + static_cast<std::ptrdiff_t>(barWidth), '#');
    }

    if (region.muted && !contents.empty()) {
        contents[0] = 'M';
    }

    const auto duration = region.clip.sampleRate == 0
        ? 0.0
        : static_cast<double>(region.clip.samples.size()) / static_cast<double>(region.clip.sampleRate);

    std::cout << "region: " << region.name;
    if (&region == selectedRegion()) {
        std::cout << " selected";
    }
    if (!region.clip.samples.empty()) {
        std::cout << " " << std::fixed << std::setprecision(2) << duration << "s";
    }
    std::cout << '\n'
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

void TerminalApp::printStatus() const
{
    const auto& settings = audio_.settings();
    std::cout
        << "project: " << (activeProject_ ? activeProject_->name : "none") << '\n'
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
    loadProjectRegions();

    try {
        const auto preset = presets_.loadPreset(currentPresetCategory_, currentPresetId_);
        printResult(audio_.applyPreset(preset));
        currentPresetUsesMidi_ = preset.source == "midi";
    } catch (const std::exception& ex) {
        std::cout << "warning: " << ex.what() << '\n';
    }
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
    regions_.push_back(std::move(region));
    selectedRegionIndex_ = regions_.size() - 1;
    saveProjectRegions();
    printSelectedRegion();
}

void TerminalApp::selectRegion(const std::string& name)
{
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        if (regions_[index].name == name) {
            selectedRegionIndex_ = index;
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
    printRegion(*region);
}

void TerminalApp::recordSelectedRegion()
{
    auto* region = selectedRegion();
    if (region == nullptr) {
        std::cout << "error: Create or select a region first.\n";
        return;
    }

    const auto result = audio_.beginRecording();
    printResult(result);
    if (!result.ok) {
        return;
    }

    waitForSpace();
    region->clip = audio_.finishRecording();
    saveRegionAudio(*region);
    saveProjectRegions();
    std::cout << "ok: Recorded " << region->clip.samples.size() << " samples into " << region->name << ".\n";
    printRegion(*region);
}

void TerminalApp::playRegions()
{
    std::vector<audio::AudioClip> clips;
    for (const auto& region : regions_) {
        if (!region.muted && !region.clip.samples.empty()) {
            clips.push_back(region.clip);
        }
    }

    printResult(audio_.playClips(std::move(clips)));
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
    } else if (command == "record" && tokens.size() == 1) {
        recordSelectedRegion();
    } else if (command == "play" && tokens.size() == 1) {
        playRegions();
    } else if (command == "mute" && tokens.size() == 2 && tokens[1] == "region") {
        muteSelectedRegion();
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
    } else if (command == "start") {
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
