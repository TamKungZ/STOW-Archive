#define UNICODE
#define _UNICODE

#include <iterator>
#include <windows.h>

namespace {

constexpr wchar_t kWindowClass[] = L"STOWArchiveWindow";

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC deviceContext = BeginPaint(window, &paint);
        const wchar_t text[] = L"Hello from native Windows UI";
        TextOutW(deviceContext, 24, 24, text, static_cast<int>(std::size(text) - 1));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    const WNDCLASSW windowClass{
        .lpfnWndProc = WindowProc,
        .hInstance = instance,
        .hCursor = LoadCursorW(nullptr, IDC_ARROW),
        .lpszClassName = kWindowClass,
    };

    if (!RegisterClassW(&windowClass)) {
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        kWindowClass,
        L"STOW Archive",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        480,
        320,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (window == nullptr) {
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
