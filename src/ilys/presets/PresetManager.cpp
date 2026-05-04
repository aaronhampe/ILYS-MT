#include "ilys/presets/PresetManager.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace ilys::presets {
namespace {

float readFloat(const nlohmann::json& json, const char* key, float fallback)
{
    return json.contains(key) ? json.at(key).get<float>() : fallback;
}

std::string readString(const nlohmann::json& json, const char* key, std::string fallback = {})
{
    return json.contains(key) ? json.at(key).get<std::string>() : std::move(fallback);
}

bool isSafeSegment(const std::string& segment)
{
    return !segment.empty()
        && segment != "."
        && segment != ".."
        && segment.find('/') == std::string::npos
        && segment.find('\\') == std::string::npos;
}

InstrumentPreset parsePreset(const std::filesystem::path& path, const std::string& category)
{
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("Could not open preset file: " + path.string());
    }

    nlohmann::json json;
    input >> json;

    InstrumentPreset preset;
    preset.id = readString(json, "id", path.stem().string());
    preset.name = readString(json, "name", preset.id);
    preset.category = readString(json, "category", category);
    preset.source = readString(json, "source", category == "guitar" ? "audio" : "midi");
    preset.description = readString(json, "description");
    preset.inputChannel = json.value("input_channel", 0U);
    preset.inputGainDb = readFloat(json, "input_gain_db", preset.inputGainDb);
    preset.outputGainDb = readFloat(json, "output_gain_db", preset.outputGainDb);
    preset.highPassHz = readFloat(json, "high_pass_hz", preset.highPassHz);
    preset.lowPassHz = readFloat(json, "low_pass_hz", preset.lowPassHz);
    preset.gateThresholdDb = readFloat(json, "gate_threshold_db", preset.gateThresholdDb);
    preset.drive = readFloat(json, "drive", preset.drive);
    preset.waveform = readString(json, "waveform", preset.waveform);
    preset.maxVoices = json.value("max_voices", preset.maxVoices);
    preset.attackMs = readFloat(json, "attack_ms", preset.attackMs);
    preset.decayMs = readFloat(json, "decay_ms", preset.decayMs);
    preset.sustain = readFloat(json, "sustain", preset.sustain);
    preset.releaseMs = readFloat(json, "release_ms", preset.releaseMs);
    preset.tone = readFloat(json, "tone", preset.tone);
    preset.detuneCents = readFloat(json, "detune_cents", preset.detuneCents);
    preset.stereoWidth = readFloat(json, "stereo_width", preset.stereoWidth);
    preset.tremoloDepth = readFloat(json, "tremolo_depth", preset.tremoloDepth);
    preset.tremoloRateHz = readFloat(json, "tremolo_rate_hz", preset.tremoloRateHz);

    return preset;
}

} // namespace

PresetManager::PresetManager(std::filesystem::path root)
    : root_{std::move(root)}
{
}

const std::filesystem::path& PresetManager::root() const noexcept
{
    return root_;
}

std::vector<std::string> PresetManager::listCategories() const
{
    std::vector<std::string> categories;
    if (!std::filesystem::exists(root_)) {
        return categories;
    }

    for (const auto& entry : std::filesystem::directory_iterator(root_)) {
        if (entry.is_directory()) {
            categories.push_back(entry.path().filename().string());
        }
    }

    std::sort(categories.begin(), categories.end());
    return categories;
}

std::vector<InstrumentPreset> PresetManager::listPresets(const std::string& category) const
{
    if (!isSafeSegment(category)) {
        throw std::runtime_error("Invalid preset category: " + category);
    }

    std::vector<InstrumentPreset> presets;
    const auto folder = root_ / category;
    if (!std::filesystem::exists(folder)) {
        return presets;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            presets.push_back(parsePreset(entry.path(), category));
        }
    }

    std::sort(presets.begin(), presets.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return presets;
}

InstrumentPreset PresetManager::loadPreset(const std::string& category, const std::string& id) const
{
    if (!isSafeSegment(category) || !isSafeSegment(id)) {
        throw std::runtime_error("Invalid preset path: " + category + "/" + id);
    }

    const auto path = root_ / category / (id + ".json");
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Unknown preset: " + category + "/" + id);
    }

    return parsePreset(path, category);
}

} // namespace ilys::presets
