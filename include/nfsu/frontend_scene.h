#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace nfsu {
bool load_frontend_scene(const std::filesystem::path& root,
    std::vector<unsigned char>& rgba, unsigned width, unsigned height,
    std::string& error);
}
