#include "CommandLine.h"

namespace Prism {

    static std::vector<std::string>& Storage() {
        static std::vector<std::string> args;
        return args;
    }

    void SetLaunchArguments(int argc, char** argv) {
        Storage().clear();
        for (int i = 1; i < argc; i++) // 0 = nome do executavel
            Storage().emplace_back(argv[i]);
    }

    const std::vector<std::string>& GetLaunchArguments() {
        return Storage();
    }

}
