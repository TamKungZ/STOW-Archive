#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>

#include "stow_archive.hpp"

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"STOWArchiveWindow";
constexpr wchar_t kCompressionWindowClass[] = L"STOWCompressionProgressWindow";
constexpr UINT kCompressionFinishedMessage = WM_APP + 1;
std::wstring g_viewText;

struct CompressionContext {
    HWND window = nullptr;
    std::filesystem::path input;
    std::filesystem::path output;
    std::string error;
    bool succeeded = false;
};

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

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size == 0) {
        return L"The archive command returned invalid UTF-8 output.";
    }
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

void ShowError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"STOW", MB_OK | MB_ICONERROR);
}

LRESULT CALLBACK CompressionProgressProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(
            window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        const auto* context = static_cast<const CompressionContext*>(create->lpCreateParams);
        const std::wstring label =
            L"Compressing " + context->input.filename().wstring() + L"...";
        CreateWindowExW(
            0, L"STATIC", label.c_str(), WS_CHILD | WS_VISIBLE,
            20, 18, 410, 24, window, nullptr, create->hInstance, nullptr);
        HWND progress = CreateWindowExW(
            0, PROGRESS_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
            20, 52, 410, 20, window, nullptr, create->hInstance, nullptr);
        if (progress == nullptr) {
            return -1;
        }
        SendMessageW(progress, PBM_SETMARQUEE, TRUE, 30);
        CreateWindowExW(
            0, L"STATIC", L"Please wait while STOW creates the archive.",
            WS_CHILD | WS_VISIBLE,
            20, 82, 410, 22, window, nullptr, create->hInstance, nullptr);
        return 0;
    }
    case kCompressionFinishedMessage: {
        auto* context = reinterpret_cast<CompressionContext*>(lParam);
        if (context == nullptr ||
            reinterpret_cast<CompressionContext*>(GetWindowLongPtrW(window, GWLP_USERDATA)) !=
                context) {
            return 0;
        }
        if (context->succeeded) {
            MessageBoxW(
                window,
                L"Archive created successfully.",
                L"STOW",
                MB_OK | MB_ICONINFORMATION);
        } else {
            ShowError(Utf8ToWide(context->error));
        }
        DestroyWindow(window);
        return 0;
    }
    case WM_CLOSE:
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

DWORD WINAPI CompressOnWorkerThread(void* parameter) {
    auto* context = static_cast<CompressionContext*>(parameter);
    context->succeeded =
        stow_archive::Compress(context->output, {context->input}, context->error);
    SendMessageW(
        context->window,
        kCompressionFinishedMessage,
        0,
        reinterpret_cast<LPARAM>(context));
    return 0;
}

bool RunCompressionWithProgress(
    const std::filesystem::path& input, const std::filesystem::path& output) {
    INITCOMMONCONTROLSEX commonControls{
        .dwSize = sizeof(INITCOMMONCONTROLSEX),
        .dwICC = ICC_PROGRESS_CLASS,
    };
    if (!InitCommonControlsEx(&commonControls)) {
        ShowError(L"Could not initialize the compression progress indicator.");
        return false;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass{
        .lpfnWndProc = CompressionProgressProc,
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kCompressionWindowClass,
    };
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ShowError(L"Could not register the compression progress window.");
        return false;
    }

    constexpr int kWindowWidth = 470;
    constexpr int kWindowHeight = 155;
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    CompressionContext context{
        .input = input,
        .output = output,
    };
    context.window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kCompressionWindowClass,
        L"STOW - Compressing",
        WS_CAPTION | WS_SYSMENU,
        (screenWidth - kWindowWidth) / 2,
        (screenHeight - kWindowHeight) / 2,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        &context);
    if (context.window == nullptr) {
        ShowError(L"Could not create the compression progress window.");
        return false;
    }

    HANDLE worker = CreateThread(
        nullptr, 0, CompressOnWorkerThread, &context, 0, nullptr);
    if (worker == nullptr) {
        DestroyWindow(context.window);
        ShowError(L"Could not start the compression task.");
        return false;
    }

    ShowWindow(context.window, SW_SHOW);
    UpdateWindow(context.window);

    MSG message{};
    int messageResult = 0;
    while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    return messageResult >= 0 && context.succeeded;
}

bool GetSelectedFile(const std::vector<std::wstring>& arguments, std::wstring& path) {
    if (arguments.size() != 3 || GetFileAttributesW(arguments[2].c_str()) == INVALID_FILE_ATTRIBUTES) {
        ShowError(L"Select one existing file or folder.");
        return false;
    }
    path = arguments[2];
    return true;
}

int ShowArchiveViewer(const std::wstring& archivePath, HINSTANCE instance, int showCommand) {
    std::string listing;
    std::string error;
    if (!stow_archive::List(std::filesystem::path(archivePath), listing, error)) {
        ShowError(Utf8ToWide(error));
        return 1;
    }

    g_viewText = Utf8ToWide(listing);
    if (g_viewText.empty()) {
        g_viewText = L"(empty archive)";
    }

    const WNDCLASSW windowClass{
        .lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
            switch (message) {
            case WM_CREATE:
                CreateWindowExW(
                    WS_EX_CLIENTEDGE,
                    L"EDIT",
                    g_viewText.c_str(),
                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                        ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
                    0,
                    0,
                    0,
                    0,
                    window,
                    nullptr,
                    GetModuleHandleW(nullptr),
                    nullptr);
                return 0;
            case WM_SIZE: {
                HWND edit = FindWindowExW(window, nullptr, L"EDIT", nullptr);
                if (edit != nullptr) {
                    MoveWindow(edit, 12, 12, LOWORD(lParam) - 24, HIWORD(lParam) - 24, TRUE);
                }
                return 0;
            }
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(window, message, wParam, lParam);
            }
        },
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kWindowClass,
    };

    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ShowError(L"Could not register the STOW viewer window.");
        return 1;
    }

    const std::wstring title = L"STOW - " + std::filesystem::path(archivePath).filename().wstring();
    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        480,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        ShowError(L"Could not create the STOW viewer window.");
        return 1;
    }

    ShowWindow(window, showCommand);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

int CompressFile(const std::wstring& inputPath) {
    const std::filesystem::path input(inputPath);
    std::vector<wchar_t> outputPath(32768);
    const std::wstring defaultName = input.stem().wstring() + L".sto";
    if (defaultName.size() >= outputPath.size()) {
        ShowError(L"The selected file name is too long.");
        return 1;
    }
    std::copy(defaultName.begin(), defaultName.end(), outputPath.begin());

    const std::wstring initialDirectory = input.parent_path().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"STOW archives (*.sto)\0*.sto\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = outputPath.data();
    dialog.nMaxFile = static_cast<DWORD>(outputPath.size());
    dialog.lpstrInitialDir = initialDirectory.empty() ? nullptr : initialDirectory.c_str();
    dialog.lpstrDefExt = L"sto";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&dialog)) {
        return CommDlgExtendedError() == 0 ? 0 : 1;
    }
    if (SamePath(inputPath, outputPath.data())) {
        ShowError(L"The archive output cannot overwrite the selected input.");
        return 1;
    }

    return RunCompressionWithProgress(
        std::filesystem::path(inputPath),
        std::filesystem::path(outputPath.data()))
        ? 0
        : 1;
}

int ExtractFile(const std::wstring& archivePath) {
    const std::filesystem::path archive(archivePath);
    const DWORD attributes = GetFileAttributesW(archivePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        ShowError(L"Select an existing STOW archive file.");
        return 1;
    }

    const std::filesystem::path destination = archive.parent_path() / archive.stem();
    if (!CreateDirectoryW(destination.c_str(), nullptr)) {
        ShowError(GetLastError() == ERROR_ALREADY_EXISTS
            ? L"The extraction folder already exists. Rename or move it first."
            : L"Could not create the extraction folder.");
        return 1;
    }

    std::string error;
    if (!stow_archive::Extract(
            std::filesystem::path(archivePath), destination, error)) {
        ShowError(Utf8ToWide(error));
        return 1;
    }
    MessageBoxW(
        nullptr,
        (L"Extracted to:\n" + destination.wstring()).c_str(),
        L"STOW",
        MB_OK | MB_ICONINFORMATION);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    int argumentCount = 0;
    wchar_t** rawArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (rawArguments == nullptr) {
        ShowError(L"Could not read the command-line arguments.");
        return 1;
    }
    std::vector<std::wstring> arguments(rawArguments, rawArguments + argumentCount);
    LocalFree(rawArguments);

    if (arguments.size() == 3 && arguments[1] == L"--open") {
        std::wstring path;
        if (!GetSelectedFile(arguments, path)) {
            return 1;
        }
        return ShowArchiveViewer(path, instance, showCommand);
    }
    if (arguments.size() == 3 && arguments[1] == L"--compress") {
        std::wstring path;
        return GetSelectedFile(arguments, path) ? CompressFile(path) : 1;
    }
    if (arguments.size() == 3 && arguments[1] == L"--extract") {
        std::wstring path;
        return GetSelectedFile(arguments, path) ? ExtractFile(path) : 1;
    }

    ShowError(L"Run stow-cli.exe install-context-menu to add STOW to File Explorer.");
    return 1;
}
