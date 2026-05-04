#include "ilys/cli/TerminalApp.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

namespace ilys::cli {
namespace {

std::vector<std::string> split(const std::string& line)
{
    std::istringstream stream{line};
    std::vector<std::string> tokens;
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }

    return tokens;
}

void printResult(const core::Result& result)
{
    std::cout << (result.ok ? "ok: " : "error: ") << result.message << '\n';
}

} // namespace

TerminalApp::TerminalApp(std::filesystem::path presetRoot)
    : presets_{std::move(presetRoot)}
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

    try {
        const auto preset = presets_.loadPreset(currentPresetCategory_, currentPresetId_);
        printResult(audio_.applyPreset(preset));
        currentPresetUsesMidi_ = preset.source == "midi";
    } catch (const std::exception& ex) {
        std::cout << "warning: " << ex.what() << '\n';
    }

    std::string line;
    while (std::cout << "ilys-mt> " && std::getline(std::cin, line)) {
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
        << "Welcome. Type 'help' to see commands.\n"
        << "Preset root: " << presets_.root() << "\n\n";
}

void TerminalApp::printHelp() const
{
    std::cout
        << "commands:\n"
        << "  help                  show this help\n"
        << "  devices               list audio inputs and outputs\n"
        << "  midi                  list MIDI input devices\n"
        << "  input <index>         select audio input device\n"
        << "  output <index>        select audio output device\n"
        << "  midi input <index>    select MIDI input device\n"
        << "  presets               list preset categories\n"
        << "  presets <category>    list presets for a category\n"
        << "  load <category> <id>  load an instrument preset\n"
        << "  start                 start live monitoring\n"
        << "  stop                  stop live monitoring\n"
        << "  status                show current engine state\n"
        << "  quit                  exit\n";
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

void TerminalApp::printStatus() const
{
    const auto& settings = audio_.settings();
    std::cout
        << "state: " << (audio_.isRunning() ? "monitoring" : "stopped") << '\n'
        << "preset: " << currentPresetCategory_ << "/" << currentPresetId_ << '\n'
        << "source: " << (currentPresetUsesMidi_ ? "midi instrument" : "audio monitor") << '\n'
        << "input: " << (settings.inputDeviceIndex ? std::to_string(*settings.inputDeviceIndex) : "default") << '\n'
        << "output: " << (settings.outputDeviceIndex ? std::to_string(*settings.outputDeviceIndex) : "default") << '\n'
        << "midi input: " << (midi_.selectedInputIndex() ? std::to_string(*midi_.selectedInputIndex()) : "default") << '\n'
        << "midi: " << (midi_.isRunning() ? "running" : "stopped") << '\n'
        << "sample rate: " << settings.sampleRate << '\n'
        << "buffer: " << settings.framesPerBuffer << " frames\n";
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

bool TerminalApp::execute(const std::string& line)
{
    const auto tokens = split(line);
    if (tokens.empty()) {
        return true;
    }

    const auto& command = tokens[0];
    try {
        if (command == "help") {
            printHelp();
        } else if (command == "devices") {
            printDevices();
            printMidiDevices();
        } else if (command == "midi" && tokens.size() == 1) {
            printMidiDevices();
        } else if (command == "input" && tokens.size() == 2) {
            printResult(audio_.setInputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
        } else if (command == "output" && tokens.size() == 2) {
            printResult(audio_.setOutputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
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
            std::cout << "unknown command. Type 'help'.\n";
        }
    } catch (const std::exception& ex) {
        std::cout << "error: " << ex.what() << '\n';
    }

    return true;
}

} // namespace ilys::cli
