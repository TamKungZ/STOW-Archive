#define UNICODE
#define _UNICODE

#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include "stow_archive.hpp"

#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::wstring QuoteArgument(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
        } else if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
        } else {
            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(character);
        }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::wstring GetModulePath() {
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return length == 0 || length >= path.size()
        ? std::wstring{}
        : std::wstring(path.data(), length);
}

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0) {
        return L"STOW returned invalid UTF-8 output.";
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
    return result;
}

bool SamePath(const std::wstring& left, const std::wstring& right) {
    std::vector<wchar_t> leftFullPath(32768);
    std::vector<wchar_t> rightFullPath(32768);
    const DWORD leftLength = GetFullPathNameW(
        left.c_str(), static_cast<DWORD>(leftFullPath.size()), leftFullPath.data(), nullptr);
    const DWORD rightLength = GetFullPathNameW(
        right.c_str(), static_cast<DWORD>(rightFullPath.size()), rightFullPath.data(), nullptr);
    return leftLength != 0 && rightLength != 0 &&
        leftLength < leftFullPath.size() && rightLength < rightFullPath.size() &&
        CompareStringOrdinal(
            leftFullPath.data(), -1, rightFullPath.data(), -1, TRUE) == CSTR_EQUAL;
}

bool SetStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    return RegSetValueExW(
        key,
        name,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()),
        bytes) == ERROR_SUCCESS;
}

bool SetCommandValue(const std::wstring& keyPath, const std::wstring& command) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            keyPath.c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const bool success = SetStringValue(key, nullptr, command);
    RegCloseKey(key);
    return success;
}

bool CreateMenu(const std::wstring& baseKey, const std::wstring& appPath) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            baseKey.c_str(),
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_WRITE,
            nullptr,
            &key,
            nullptr) != ERROR_SUCCESS) {
        return false;
    }

    const bool metadataSet =
        SetStringValue(key, L"MUIVerb", L"Stow") &&
        SetStringValue(key, L"SubCommands", L"") &&
        SetStringValue(key, L"Icon", appPath);
    RegCloseKey(key);
    if (!metadataSet) {
        return false;
    }

    const std::wstring shellKey = baseKey + L"\\shell\\";
    const std::wstring menuActions[] = {L"open", L"compress", L"extract"};
    const std::wstring labels[] = {
        L"Open side-by-side",
        L"Compress...",
        L"Decompress",
    };
    const std::wstring switches[] = {L"--open", L"--compress", L"--extract"};
    for (size_t i = 0; i < std::size(menuActions); ++i) {
        HKEY actionKey = nullptr;
        const std::wstring actionPath = shellKey + menuActions[i];
        if (RegCreateKeyExW(
                HKEY_CURRENT_USER,
                actionPath.c_str(),
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                KEY_WRITE,
                nullptr,
                &actionKey,
                nullptr) != ERROR_SUCCESS) {
            return false;
        }
        const bool labelSet = SetStringValue(actionKey, L"MUIVerb", labels[i]);
        RegCloseKey(actionKey);
        if (!labelSet) {
            return false;
        }
        const std::wstring commandPath = actionPath + L"\\command";
        const std::wstring command = QuoteArgument(appPath) + L" " + switches[i] + L" \"%1\"";
        if (!SetCommandValue(commandPath, command)) {
            return false;
        }
    }
    return true;
}

bool InstallContextMenu(const std::wstring& appPath) {
    const std::wstring keys[] = {
        L"Software\\Classes\\*\\shell\\STOWArchive",
        L"Software\\Classes\\Directory\\shell\\STOWArchive",
    };
    for (const auto& key : keys) {
        if (!CreateMenu(key, appPath)) {
            for (const auto& cleanupKey : keys) {
                RegDeleteTreeW(HKEY_CURRENT_USER, cleanupKey.c_str());
            }
            return false;
        }
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return true;
}

bool UninstallContextMenu() {
    const std::wstring keys[] = {
        L"Software\\Classes\\*\\shell\\STOWArchive",
        L"Software\\Classes\\Directory\\shell\\STOWArchive",
    };
    bool success = true;
    for (const auto& key : keys) {
        const LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
            success = false;
        }
    }
    if (success) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    return success;
}

void PrintUsage() {
    std::wcerr
        << L"STOW command line\n"
        << L"  stow-cli compress <out.sto> <path>...\n"
        << L"  stow-cli decompress <archive.sto> [output-dir]\n"
        << L"  stow-cli list <archive.sto>\n"
        << L"  stow-cli install-context-menu [path-to-stow.exe]\n"
        << L"  stow-cli uninstall-context-menu\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        PrintUsage();
        return 2;
    }

    const std::wstring command = argv[1];
    if (command == L"install-context-menu") {
        if (argc > 3) {
            PrintUsage();
            return 2;
        }
        const std::filesystem::path appPath = argc == 3
            ? std::filesystem::path(argv[2])
            : std::filesystem::path(GetModulePath()).parent_path() / L"stow.exe";
        if (GetFileAttributesW(appPath.c_str()) == INVALID_FILE_ATTRIBUTES ||
            !InstallContextMenu(std::filesystem::absolute(appPath).wstring())) {
            std::wcerr << L"error: Could not install the File Explorer menu.\n";
            return 1;
        }
        std::wcout << L"Installed the Stow context menu for the current user.\n";
        return 0;
    }
    if (command == L"uninstall-context-menu") {
        if (argc != 2) {
            PrintUsage();
            return 2;
        }
        if (!UninstallContextMenu()) {
            std::wcerr << L"error: Could not remove the File Explorer menu.\n";
            return 1;
        }
        std::wcout << L"Removed the Stow context menu for the current user.\n";
        return 0;
    }

    if (command == L"compress" && argc >= 4) {
        std::vector<std::filesystem::path> inputs;
        for (int i = 3; i < argc; ++i) {
            if (SamePath(argv[2], argv[i])) {
                std::wcerr << L"error: The archive output cannot overwrite an input file.\n";
                return 2;
            }
            inputs.emplace_back(argv[i]);
        }
        std::string error;
        if (!stow_archive::Compress(argv[2], inputs, error)) {
            std::wcerr << L"error: " << Utf8ToWide(error) << L'\n';
            return 1;
        }
        return 0;
    } else if (command == L"decompress" && (argc == 3 || argc == 4)) {
        const std::filesystem::path archive(argv[2]);
        const std::filesystem::path destination = argc == 4
            ? std::filesystem::path(argv[3])
            : archive.parent_path() / archive.stem();
        std::error_code filesystemError;
        if (std::filesystem::exists(destination, filesystemError)) {
            std::wcerr << L"error: output directory already exists: "
                       << destination.wstring() << L'\n';
            return 1;
        }
        std::string error;
        if (!stow_archive::Extract(archive, destination, error)) {
            std::wcerr << L"error: " << Utf8ToWide(error) << L'\n';
            return 1;
        }
        return 0;
    } else if (command == L"list" && argc == 3) {
        std::string listing;
        std::string error;
        if (!stow_archive::List(argv[2], listing, error)) {
            std::wcerr << L"error: " << Utf8ToWide(error) << L'\n';
            return 1;
        }
        std::cout << listing;
        return 0;
    } else {
        PrintUsage();
        return 2;
    }
}
