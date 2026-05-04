#pragma once

#include "ilys/presets/GuitarPreset.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ilys::presets {

class PresetManager {
public:
    explicit PresetManager(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::vector<std::string> listCategories() const;
    [[nodiscard]] std::vector<GuitarPreset> listGuitarPresets() const;
    [[nodiscard]] GuitarPreset loadGuitarPreset(const std::string& id) const;

private:
    std::filesystem::path root_;
};

} // namespace ilys::presets

