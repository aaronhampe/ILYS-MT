#include "ilys/cli/TerminalApp.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        const auto workspaceRoot = std::filesystem::current_path();
        auto presetRoot = workspaceRoot / "presets";
        if (argc > 1) {
            presetRoot = std::filesystem::path(argv[1]);
        }

        const auto executablePath = std::filesystem::path{argv[0]};
        const auto executableDir = executablePath.has_parent_path()
            ? std::filesystem::absolute(executablePath.parent_path())
            : workspaceRoot;
#if defined(_WIN32)
        const auto audiovisualizerExecutable = executableDir / "ilys-audiovisualizer.exe";
#else
        const auto audiovisualizerExecutable = executableDir / "ilys-audiovisualizer";
#endif

        ilys::cli::TerminalApp app{presetRoot, workspaceRoot / "projects", audiovisualizerExecutable};
        return app.run();
    } catch (const std::exception& ex) {
        std::cerr << "ILYS-MT failed: " << ex.what() << '\n';
        return 1;
    }
}
