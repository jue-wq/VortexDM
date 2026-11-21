#include "vortex/download_engine.hpp"
#include <filesystem>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: vortex-cli <http(s)-url> <destination>\n";
        return 2;
    }
    auto result = vortex::download_file(argv[1], std::filesystem::path(argv[2]));
    if (!result) { std::cerr << result.error().code << ": " << result.error().message << '\n'; return 1; }
    std::cout << "Download completed: " << argv[2] << '\n';
    return 0;
}
