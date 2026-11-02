#include "stow_archive.hpp"

#include <stow.h>

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>

namespace stow_archive {
namespace {

struct StowBufferDeleter {
    size_t length;

    void operator()(uint8_t* data) const {
        stow_buffer_free(data, length);
    }
};

std::string PathToUtf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::string ErrorMessage(int error) {
    const char* message = stow_error_message(error);
    return message == nullptr ? "unknown STOW error" : message;
}

bool Fail(int error, const std::string& context, std::string& output) {
    output = context + ": " + ErrorMessage(error);
    return false;
}

std::string EntryName(const std::filesystem::path& path) {
    std::string name = PathToUtf8(path);
    for (char& character : name) {
        if (character == '\\') {
            character = '/';
        }
    }
    return name;
}

bool AddPath(StowWriter* writer, const std::filesystem::path& input,
             const std::filesystem::path& archiveName, std::string& error) {
    std::error_code filesystemError;
    const auto attributes = std::filesystem::symlink_status(input, filesystemError);
    if (filesystemError) {
        error = PathToUtf8(input) + ": " + filesystemError.message();
        return false;
    }

    if (std::filesystem::is_symlink(attributes)) {
        error = PathToUtf8(input) + ": symbolic links are not supported";
        return false;
    }

    if (std::filesystem::is_directory(attributes)) {
        std::vector<std::filesystem::path> children;
        for (std::filesystem::directory_iterator it(input, filesystemError), end;
             !filesystemError && it != end; it.increment(filesystemError)) {
            children.push_back(it->path());
        }
        if (filesystemError) {
            error = PathToUtf8(input) + ": " + filesystemError.message();
            return false;
        }
        std::sort(children.begin(), children.end());
        for (const auto& child : children) {
            if (!AddPath(writer, child, archiveName / child.filename(), error)) {
                return false;
            }
        }
        return true;
    }

    if (!std::filesystem::is_regular_file(attributes)) {
        error = PathToUtf8(input) + ": not a regular file";
        return false;
    }

    const std::string inputUtf8 = PathToUtf8(input);
    const std::string nameUtf8 = EntryName(archiveName);
    const int result = stow_writer_add_file(writer, inputUtf8.c_str(), nameUtf8.c_str());
    return result == STOW_OK || Fail(result, PathToUtf8(input), error);
}

bool IsSafeEntryName(const char* rawName, std::filesystem::path& relativePath) {
    if (rawName == nullptr) {
        return false;
    }
    const std::string_view name(rawName);
    if (name.empty() || name.find('\\') != std::string_view::npos) {
        return false;
    }

    relativePath = std::filesystem::u8path(name);
    if (relativePath.empty() || relativePath.is_absolute() || relativePath.has_root_name() ||
        relativePath.has_root_directory()) {
        return false;
    }
    for (const auto& component : relativePath) {
        if (component == "." || component == "..") {
            return false;
        }
    }
    return true;
}

} // namespace

bool Compress(const std::filesystem::path& output,
              const std::vector<std::filesystem::path>& inputs, std::string& error) {
    if (inputs.empty()) {
        error = "no input paths were provided";
        return false;
    }
    const std::string outputUtf8 = PathToUtf8(output);
    std::error_code filesystemError;
    const auto absoluteOutput = std::filesystem::absolute(output, filesystemError).lexically_normal();
    if (filesystemError) {
        error = PathToUtf8(output) + ": " + filesystemError.message();
        return false;
    }
    for (const auto& input : inputs) {
        const auto absoluteInput = std::filesystem::absolute(input, filesystemError).lexically_normal();
        if (filesystemError) {
            error = PathToUtf8(input) + ": " + filesystemError.message();
            return false;
        }
        const auto relativeOutput = absoluteOutput.lexically_relative(absoluteInput);
        const bool inputIsDirectory = std::filesystem::is_directory(input, filesystemError);
        if (filesystemError) {
            error = PathToUtf8(input) + ": " + filesystemError.message();
            return false;
        }
        if (absoluteInput == absoluteOutput ||
            (inputIsDirectory && !relativeOutput.empty() &&
             !relativeOutput.is_absolute() &&
             *relativeOutput.begin() != std::filesystem::path(".."))) {
            error = "archive output cannot overwrite an input";
            return false;
        }
        filesystemError.clear();
    }

    std::unique_ptr<StowWriter, decltype(&stow_writer_free)> writer(
        stow_writer_new(), &stow_writer_free);
    if (!writer) {
        error = "could not allocate a STOW writer";
        return false;
    }

    for (const auto& input : inputs) {
        if (!AddPath(writer.get(), input, input.filename(), error)) {
            return false;
        }
    }

    const int result = stow_writer_save(writer.get(), outputUtf8.c_str());
    return result == STOW_OK || Fail(result, PathToUtf8(output), error);
}

bool List(const std::filesystem::path& archive, std::string& listing, std::string& error) {
    const std::string archiveUtf8 = PathToUtf8(archive);
    StowReader* rawReader = nullptr;
    const int openResult = stow_reader_open_file(archiveUtf8.c_str(), &rawReader);
    if (openResult != STOW_OK) {
        return Fail(openResult, PathToUtf8(archive), error);
    }
    std::unique_ptr<StowReader, decltype(&stow_reader_free)> reader(
        rawReader, &stow_reader_free);

    std::ostringstream stream;
    stream << std::right << std::setw(12) << "size" << "  " << "file\n";
    for (size_t index = 0; index < stow_reader_count(reader.get()); ++index) {
        const char* name = stow_reader_name(reader.get(), index);
        if (name == nullptr) {
            error = "STOW returned an invalid entry name";
            return false;
        }
        stream << std::right << std::setw(12) << stow_reader_size(reader.get(), index)
               << "  " << name << '\n';
    }
    listing = stream.str();
    return true;
}

bool Extract(const std::filesystem::path& archive,
             const std::filesystem::path& destination, std::string& error) {
    const std::string archiveUtf8 = PathToUtf8(archive);
    StowReader* rawReader = nullptr;
    const int openResult = stow_reader_open_file(archiveUtf8.c_str(), &rawReader);
    if (openResult != STOW_OK) {
        return Fail(openResult, PathToUtf8(archive), error);
    }
    std::unique_ptr<StowReader, decltype(&stow_reader_free)> reader(
        rawReader, &stow_reader_free);

    std::error_code filesystemError;
    std::filesystem::create_directories(destination, filesystemError);
    if (filesystemError) {
        error = PathToUtf8(destination) + ": " + filesystemError.message();
        return false;
    }

    for (size_t index = 0; index < stow_reader_count(reader.get()); ++index) {
        std::filesystem::path relativePath;
        if (!IsSafeEntryName(stow_reader_name(reader.get(), index), relativePath)) {
            error = "archive contains an unsafe entry path";
            return false;
        }
        const auto target = destination / relativePath;
        std::filesystem::create_directories(target.parent_path(), filesystemError);
        if (filesystemError) {
            error = PathToUtf8(target.parent_path()) + ": " + filesystemError.message();
            return false;
        }
        if (std::filesystem::exists(target, filesystemError)) {
            error = PathToUtf8(target) + ": output file already exists";
            return false;
        }
        if (filesystemError) {
            error = PathToUtf8(target) + ": " + filesystemError.message();
            return false;
        }

        uint8_t* data = nullptr;
        size_t length = 0;
        const int readResult = stow_reader_read(reader.get(), index, &data, &length);
        if (readResult != STOW_OK) {
            return Fail(readResult, PathToUtf8(target), error);
        }
        std::unique_ptr<uint8_t, StowBufferDeleter> buffer(data, StowBufferDeleter{length});
        std::ofstream output(target, std::ios::binary);
        if (!output) {
            error = PathToUtf8(target) + ": could not create output file";
            return false;
        }
        if (length > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
            error = PathToUtf8(target) + ": file is too large to write";
            return false;
        }
        if (length != 0) {
            output.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(length));
        }
        if (!output) {
            error = PathToUtf8(target) + ": failed while writing output file";
            return false;
        }
    }
    return true;
}

} // namespace stow_archive
