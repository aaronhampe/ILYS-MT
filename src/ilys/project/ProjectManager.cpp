#include "ilys/project/ProjectManager.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace ilys::project {
namespace {

constexpr auto projectFileName = "project.json";

std::string readProjectName(const std::filesystem::path& path)
{
    const auto metadataPath = path / projectFileName;
    std::ifstream input{metadataPath};
    if (!input) {
        return path.filename().string();
    }

    try {
        const auto json = nlohmann::json::parse(input);
        return json.value("name", path.filename().string());
    } catch (const std::exception&) {
        return path.filename().string();
    }
}

void writeProjectMetadata(const ProjectInfo& project)
{
    const nlohmann::json json{
        {"name", project.name},
        {"version", 1},
        {"type", "ilys-mt-project"}
    };

    std::ofstream output{project.path / projectFileName};
    output << json.dump(2) << '\n';
}

} // namespace

ProjectManager::ProjectManager(std::filesystem::path root)
    : root_{std::move(root)}
{
}

const std::filesystem::path& ProjectManager::root() const noexcept
{
    return root_;
}

std::vector<ProjectInfo> ProjectManager::listProjects() const
{
    std::vector<ProjectInfo> projects;
    if (!std::filesystem::exists(root_)) {
        return projects;
    }

    for (const auto& entry : std::filesystem::directory_iterator{root_}) {
        if (!entry.is_directory()) {
            continue;
        }

        projects.push_back(ProjectInfo{
            readProjectName(entry.path()),
            entry.path()
        });
    }

    std::sort(projects.begin(), projects.end(), [](const ProjectInfo& lhs, const ProjectInfo& rhs) {
        return lhs.name < rhs.name;
    });

    return projects;
}

std::optional<ProjectInfo> ProjectManager::findProject(const std::string& name) const
{
    for (const auto& project : listProjects()) {
        if (project.name == name || project.path.filename().string() == name) {
            return project;
        }
    }

    return std::nullopt;
}

core::Result ProjectManager::createProject(const std::string& name, ProjectInfo& project)
{
    if (!isValidProjectName(name)) {
        return core::Result::failure("Project names cannot be empty or contain path separators.");
    }

    const auto path = projectPath(name);
    if (std::filesystem::exists(path)) {
        return core::Result::failure("Project already exists: " + name);
    }

    std::filesystem::create_directories(path / "audio");
    std::filesystem::create_directories(path / "regions");
    std::filesystem::create_directories(path / "midi");
    std::filesystem::create_directories(path / "mixes");

    project = ProjectInfo{name, path};
    writeProjectMetadata(project);

    return core::Result::success("Project created: " + name);
}

core::Result ProjectManager::openProject(const std::string& name, ProjectInfo& project) const
{
    const auto found = findProject(name);
    if (!found) {
        return core::Result::failure("Project not found: " + name);
    }

    project = *found;
    return core::Result::success("Project opened: " + project.name);
}

core::Result ProjectManager::ensureAudiovisualizerFolder(std::filesystem::path& path) const
{
    path = root_.parent_path() / "audiovisualizer";
    std::filesystem::create_directories(path);
    return core::Result::success("Audiovisualizer folder ready: " + path.string());
}

bool ProjectManager::isValidProjectName(const std::string& name)
{
    return !name.empty()
        && name.find('/') == std::string::npos
        && name.find('\\') == std::string::npos;
}

std::filesystem::path ProjectManager::projectPath(const std::string& name) const
{
    return root_ / name;
}

} // namespace ilys::project
