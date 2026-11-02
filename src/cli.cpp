#include "stow_archive.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void PrintUsage() {
    std::cerr
        << "STOW command line\n"
        << "  stow-cli compress <out.sto> <path>...\n"
        << "  stow-cli decompress <archive.sto> [output-dir]\n"
        << "  stow-cli list <archive.sto>\n";
}

int Fail(const std::string& error) {
    std::cerr << "error: " << error << '\n';
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }

    const std::string command = argv[1];
    if (command == "compress" && argc >= 4) {
        std::vector<std::filesystem::path> inputs;
        for (int index = 3; index < argc; ++index) {
            inputs.emplace_back(argv[index]);
        }
        std::string error;
        return stow_archive::Compress(argv[2], inputs, error) ? 0 : Fail(error);
    }

    if (command == "decompress" && (argc == 3 || argc == 4)) {
        const std::filesystem::path archive(argv[2]);
        const std::filesystem::path destination = argc == 4
            ? std::filesystem::path(argv[3])
            : archive.parent_path() / archive.stem();
        std::error_code filesystemError;
        if (std::filesystem::exists(destination, filesystemError)) {
            return Fail("output directory already exists: " + destination.string());
        }
        if (filesystemError) {
            return Fail(destination.string() + ": " + filesystemError.message());
        }
        std::string error;
        return stow_archive::Extract(archive, destination, error) ? 0 : Fail(error);
    }

    if (command == "list" && argc == 3) {
        std::string listing;
        std::string error;
        if (!stow_archive::List(argv[2], listing, error)) {
            return Fail(error);
        }
        std::cout << listing;
        return 0;
    }

    PrintUsage();
    return 2;
}
