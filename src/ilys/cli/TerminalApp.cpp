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
}

int TerminalApp::run()
{
    printWelcome();

    try {
        const auto preset = presets_.loadGuitarPreset(currentPresetId_);
        printResult(audio_.applyPreset(preset));
    } catch (const std::exception& ex) {
        std::cout << "warning: " << ex.what() << '\n';
    }

    std::string line;
    while (std::cout << "ilys-mt> " && std::getline(std::cin, line)) {
        if (!execute(line)) {
            break;
        }
    }

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
        << "  input <index>         select audio input device\n"
        << "  output <index>        select audio output device\n"
        << "  presets guitar        list guitar presets\n"
        << "  load guitar <id>      load a guitar preset\n"
        << "  start                 start live guitar monitoring\n"
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

void TerminalApp::printStatus() const
{
    const auto& settings = audio_.settings();
    std::cout
        << "state: " << (audio_.isRunning() ? "monitoring" : "stopped") << '\n'
        << "preset: guitar/" << currentPresetId_ << '\n'
        << "input: " << (settings.inputDeviceIndex ? std::to_string(*settings.inputDeviceIndex) : "default") << '\n'
        << "output: " << (settings.outputDeviceIndex ? std::to_string(*settings.outputDeviceIndex) : "default") << '\n'
        << "sample rate: " << settings.sampleRate << '\n'
        << "buffer: " << settings.framesPerBuffer << " frames\n";
}

void TerminalApp::printGuitarPresets() const
{
    const auto guitarPresets = presets_.listGuitarPresets();
    if (guitarPresets.empty()) {
        std::cout << "no guitar presets found in " << (presets_.root() / "guitar") << '\n';
        return;
    }

    for (const auto& preset : guitarPresets) {
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
        } else if (command == "input" && tokens.size() == 2) {
            printResult(audio_.setInputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
        } else if (command == "output" && tokens.size() == 2) {
            printResult(audio_.setOutputDevice(static_cast<unsigned int>(std::stoul(tokens[1]))));
        } else if (command == "presets" && tokens.size() == 2 && tokens[1] == "guitar") {
            printGuitarPresets();
        } else if (command == "load" && tokens.size() == 3 && tokens[1] == "guitar") {
            const auto preset = presets_.loadGuitarPreset(tokens[2]);
            printResult(audio_.applyPreset(preset));
            currentPresetId_ = preset.id;
        } else if (command == "start") {
            printResult(audio_.start());
        } else if (command == "stop") {
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
