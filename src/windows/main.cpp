#define UNICODE
#define _UNICODE

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>

#include "stow_archive.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"STOWArchiveWindow";
constexpr wchar_t kCompressionSettingsWindowClass[] = L"STOWCompressionSettingsWindow";
constexpr wchar_t kCompressionWindowClass[] = L"STOWCompressionProgressWindow";
constexpr UINT kCompressionFinishedMessage = WM_APP + 1;
constexpr int kOutputPathControlId = 1001;
constexpr int kBrowseControlId = 1002;
constexpr int kStartCompressionControlId = 1003;
constexpr int kCancelCompressionControlId = 1004;
std::wstring g_viewText;

struct CompressionSettingsContext {
    std::filesystem::path input;
    std::filesystem::path output;
    bool accepted = false;
};

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

LRESULT CALLBACK CompressionSettingsProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        const LRESULT result = DefWindowProcW(window, message, wParam, lParam);
        if (result != FALSE) {
            SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        return result;
    }
    case WM_CREATE: {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        const auto* context =
            static_cast<const CompressionSettingsContext*>(create->lpCreateParams);
        const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        auto createControl = [&](DWORD extendedStyle, const wchar_t* className,
                                 const wchar_t* text, DWORD style, int x, int y,
                                 int width, int height, int id) {
            HWND control = CreateWindowExW(
                extendedStyle,
                className,
                text,
                WS_CHILD | WS_VISIBLE | style,
                x,
                y,
                width,
                height,
                window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                create->hInstance,
                nullptr);
            if (control != nullptr) {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            return control;
        };

        const std::wstring sourceLabel =
            L"Source: " + context->input.filename().wstring();
        if (createControl(0, L"STATIC", sourceLabel.c_str(), 0, 18, 14, 520, 22, 0) == nullptr ||
            createControl(0, L"STATIC", L"Archive file:", 0, 18, 48, 92, 24, 0) == nullptr) {
            return -1;
        }

        const HWND pathEdit = createControl(
            WS_EX_CLIENTEDGE,
            L"EDIT",
            context->output.wstring().c_str(),
            ES_AUTOHSCROLL | WS_TABSTOP,
            112,
            44,
            350,
            28,
            kOutputPathControlId);
        const HWND browseButton = createControl(
            0, L"BUTTON", L"Browse...", WS_TABSTOP, 470, 44, 88, 28, kBrowseControlId);
        const HWND compressButton = createControl(
            0,
            L"BUTTON",
            L"Compress",
            WS_TABSTOP | BS_DEFPUSHBUTTON,
            360,
            94,
            96,
            30,
            kStartCompressionControlId);
        const HWND cancelButton = createControl(
            0,
            L"BUTTON",
            L"Cancel",
            WS_TABSTOP,
            462,
            94,
            96,
            30,
            kCancelCompressionControlId);
        if (pathEdit == nullptr || browseButton == nullptr ||
            compressButton == nullptr || cancelButton == nullptr) {
            return -1;
        }
        SetFocus(pathEdit);
        return 0;
    }
    case WM_COMMAND: {
        const int controlId = LOWORD(wParam);
        if (controlId == kBrowseControlId) {
            std::vector<wchar_t> path(32768);
            const int length = GetWindowTextW(
                GetDlgItem(window, kOutputPathControlId),
                path.data(),
                static_cast<int>(path.size()));
            if (length <= 0) {
                ShowError(L"Enter an archive file name before browsing.");
                return 0;
            }

            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = L"STOW archives (*.sto)\0*.sto\0All files (*.*)\0*.*\0";
            dialog.lpstrFile = path.data();
            dialog.nMaxFile = static_cast<DWORD>(path.size());
            dialog.lpstrDefExt = L"sto";
            dialog.Flags =
                OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
            if (GetSaveFileNameW(&dialog)) {
                SetWindowTextW(GetDlgItem(window, kOutputPathControlId), path.data());
            } else if (CommDlgExtendedError() != 0) {
                ShowError(L"Could not open the archive save dialog.");
            }
            return 0;
        }

        if (controlId == kCancelCompressionControlId) {
            DestroyWindow(window);
            return 0;
        }

        if (controlId == kStartCompressionControlId) {
            auto* context = reinterpret_cast<CompressionSettingsContext*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            if (context == nullptr) {
                ShowError(L"Could not read the compression settings.");
                return 0;
            }

            std::vector<wchar_t> path(32768);
            const int length = GetWindowTextW(
                GetDlgItem(window, kOutputPathControlId),
                path.data(),
                static_cast<int>(path.size()));
            if (length <= 0) {
                ShowError(L"Enter a name for the archive.");
                return 0;
            }
            context->output = std::filesystem::path(path.data());
            if (context->output.extension().empty()) {
                context->output += L".sto";
            }
            if (SamePath(context->input.wstring(), context->output.wstring())) {
                ShowError(L"The archive output cannot overwrite the selected input.");
                return 0;
            }

            std::error_code filesystemError;
            const auto parent = context->output.parent_path().empty()
                ? std::filesystem::current_path(filesystemError)
                : context->output.parent_path();
            if (filesystemError || !std::filesystem::is_directory(parent, filesystemError) ||
                filesystemError) {
                ShowError(L"The selected output folder does not exist.");
                return 0;
            }
            if (std::filesystem::exists(context->output, filesystemError)) {
                if (filesystemError) {
                    ShowError(L"Could not check the selected output file.");
                    return 0;
                }
                if (MessageBoxW(
                        window,
                        L"The archive already exists. Replace it?",
                        L"STOW",
                        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                    return 0;
                }
            } else if (filesystemError) {
                ShowError(L"Could not check the selected output file.");
                return 0;
            }

            context->accepted = true;
            DestroyWindow(window);
            return 0;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool ShowCompressionSettings(
    const std::filesystem::path& input, std::filesystem::path& output) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass{
        .lpfnWndProc = CompressionSettingsProc,
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1),
        .lpszClassName = kCompressionSettingsWindowClass,
    };
    if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ShowError(L"Could not register the compression settings window.");
        return false;
    }

    CompressionSettingsContext context{
        .input = input,
        .output = input.parent_path() / (input.stem().wstring() + L".sto"),
    };
    constexpr int kWindowWidth = 590;
    constexpr int kWindowHeight = 180;
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HWND window = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kCompressionSettingsWindowClass,
        L"STOW - Compression settings",
        WS_CAPTION | WS_SYSMENU,
        (screenWidth - kWindowWidth) / 2,
        (screenHeight - kWindowHeight) / 2,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance,
        &context);
    if (window == nullptr) {
        ShowError(L"Could not create the compression settings window.");
        return false;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    MSG message{};
    int messageResult = 0;
    while ((messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (messageResult < 0) {
        ShowError(L"Could not process the compression settings window.");
        return false;
    }
    if (context.accepted) {
        output = std::move(context.output);
        return true;
    }
    return false;
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
    std::filesystem::path output;
    if (!ShowCompressionSettings(input, output)) {
        return 0;
    }

    return RunCompressionWithProgress(
        input,
        output)
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
