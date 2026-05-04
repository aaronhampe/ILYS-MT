#include "ilys/presets/PresetManager.hpp"

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

GuitarPreset parseGuitarPreset(const std::filesystem::path& path)
{
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("Could not open preset file: " + path.string());
    }

    nlohmann::json json;
    input >> json;

    GuitarPreset preset;
    preset.id = readString(json, "id", path.stem().string());
    preset.name = readString(json, "name", preset.id);
    preset.category = readString(json, "category", "guitar");
    preset.description = readString(json, "description");
    preset.inputChannel = json.value("input_channel", 0U);
    preset.inputGainDb = readFloat(json, "input_gain_db", preset.inputGainDb);
    preset.outputGainDb = readFloat(json, "output_gain_db", preset.outputGainDb);
    preset.highPassHz = readFloat(json, "high_pass_hz", preset.highPassHz);
    preset.gateThresholdDb = readFloat(json, "gate_threshold_db", preset.gateThresholdDb);
    preset.drive = readFloat(json, "drive", preset.drive);

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

    return categories;
}

std::vector<GuitarPreset> PresetManager::listGuitarPresets() const
{
    std::vector<GuitarPreset> presets;
    const auto folder = root_ / "guitar";
    if (!std::filesystem::exists(folder)) {
        return presets;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            presets.push_back(parseGuitarPreset(entry.path()));
        }
    }

    return presets;
}

GuitarPreset PresetManager::loadGuitarPreset(const std::string& id) const
{
    const auto path = root_ / "guitar" / (id + ".json");
    if (!std::filesystem::exists(path)) {
        throw std::runtime_error("Unknown guitar preset: " + id);
    }

    return parseGuitarPreset(path);
}

} // namespace ilys::presets
