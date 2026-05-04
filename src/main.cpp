#include "ilys/cli/TerminalApp.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    try {
        auto presetRoot = std::filesystem::current_path() / "presets";
        if (argc > 1) {
            presetRoot = std::filesystem::path(argv[1]);
        }

        ilys::cli::TerminalApp app{presetRoot};
        return app.run();
    } catch (const std::exception& ex) {
        std::cerr << "ILYS-MT failed: " << ex.what() << '\n';
        return 1;
    }
}

