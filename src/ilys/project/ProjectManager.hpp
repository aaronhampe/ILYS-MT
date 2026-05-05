#pragma once

#include "ilys/core/Result.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ilys::project {

struct ProjectInfo {
    std::string name;
    std::filesystem::path path;
};

class ProjectManager {
public:
    explicit ProjectManager(std::filesystem::path root);

    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::vector<ProjectInfo> listProjects() const;
    [[nodiscard]] std::optional<ProjectInfo> findProject(const std::string& name) const;

    core::Result createProject(const std::string& name, ProjectInfo& project);
    core::Result openProject(const std::string& name, ProjectInfo& project) const;
    core::Result ensureAudiovisualizerFolder(std::filesystem::path& path) const;

private:
    [[nodiscard]] static bool isValidProjectName(const std::string& name);
    [[nodiscard]] std::filesystem::path projectPath(const std::string& name) const;

    std::filesystem::path root_;
};

} // namespace ilys::project
