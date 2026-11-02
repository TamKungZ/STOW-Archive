#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace stow_archive {

bool Compress(const std::filesystem::path& output, const std::vector<std::filesystem::path>& inputs,
              std::string& error);
bool List(const std::filesystem::path& archive, std::string& listing, std::string& error);
bool Extract(const std::filesystem::path& archive, const std::filesystem::path& destination,
             std::string& error);

} // namespace stow_archive
