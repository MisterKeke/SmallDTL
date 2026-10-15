#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {
constexpr wchar_t WindowClassName[] = L"SmallDTLWindowArrangementTestWindow";
constexpr wchar_t WindowTitle[] = L"SmallDTL Test Arrangement Window";

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.lpszClassName = WindowClassName;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (!RegisterClassExW(&windowClass))
        return 1;

    HWND window = CreateWindowExW(0, WindowClassName, WindowTitle, WS_OVERLAPPEDWINDOW,
                                  64, 80, 360, 240, nullptr, nullptr, instance, nullptr);
    if (!window)
        return 2;
    ShowWindow(window, showCommand == SW_HIDE ? SW_SHOWNORMAL : showCommand);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
