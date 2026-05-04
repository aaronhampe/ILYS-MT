#pragma once

#include "ilys/presets/InstrumentPreset.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ilys::presets {

class PresetManager {
public:
    explicit PresetManager(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::vector<std::string> listCategories() const;
    [[nodiscard]] std::vector<InstrumentPreset> listPresets(const std::string& category) const;
    [[nodiscard]] InstrumentPreset loadPreset(const std::string& category, const std::string& id) const;

private:
    std::filesystem::path root_;
};

} // namespace ilys::presets
