#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <powerbase.h>
#include <string>
#include <algorithm>
#include <climits>

namespace {
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT RefreshTimer = 1;
constexpr wchar_t AppName[] = L"Battery Buddy";
constexpr wchar_t RunName[] = L"BatteryBuddy";
constexpr wchar_t RunPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
HWND mainWindow = nullptr;
HICON currentIcon = nullptr;
bool stayAwake = false;
UINT taskbarCreated = 0;

std::wstring Duration(DWORD seconds) {
    if (seconds == static_cast<DWORD>(-1)) return L"Time remaining unavailable";
    unsigned minutes = (seconds + 30) / 60;
    unsigned hours = minutes / 60;
    minutes %= 60;
    if (!hours) return L"About " + std::to_wstring(minutes) + L" min remaining";
    return L"About " + std::to_wstring(hours) + L" hr " + std::to_wstring(minutes) + L" min remaining";
}

std::wstring PowerRateLabel(const SYSTEM_BATTERY_STATE& state) {
    if (!state.BatteryPresent) return L"";
    const LONG rate = static_cast<LONG>(state.Rate);
    if (rate == LONG_MIN || (!state.Discharging && !state.Charging)) return L"";
    if ((state.Discharging && rate >= 0) || (state.Charging && rate <= 0)) return L"";
    const double watts = state.Discharging ? -static_cast<double>(rate) / 1000.0 : static_cast<double>(rate) / 1000.0;
    wchar_t text[48] = {};
    swprintf_s(text, L"%ls: %.1f W", state.Discharging ? L"Discharging" : L"Charging", watts);
    return text;
}

bool StartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RunPath, 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    bool enabled = RegQueryValueExW(key, RunName, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return enabled;
}

void SetStartup(bool enabled) {
    HKEY key = nullptr;
    LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, RunPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error == ERROR_SUCCESS) {
        if (enabled) {
            wchar_t path[32768] = {};
            if (!GetModuleFileNameW(nullptr, path, 32768)) error = GetLastError();
            else {
                std::wstring value = L"\"" + std::wstring(path) + L"\"";
                error = RegSetValueExW(key, RunName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
            }
        } else {
            error = RegDeleteValueW(key, RunName);
            if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS;
        }
        RegCloseKey(key);
    }
    if (error != ERROR_SUCCESS) MessageBoxW(mainWindow, L"Could not update the sign-in setting.", AppName, MB_ICONERROR);
}

HICON MakeIcon(const SYSTEM_POWER_STATUS& power) {
    constexpr int size = 32;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    BYTE maskBits[size * size / 8] = {};
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits);
    if (!dc || !bitmap || !mask || !pixels) {
        if (bitmap) DeleteObject(bitmap);
        if (mask) DeleteObject(mask);
        if (dc) DeleteDC(dc);
        ReleaseDC(nullptr, screen);
        return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    COLORREF background = power.BatteryLifePercent != 255 && power.BatteryLifePercent <= 20 ? RGB(145, 43, 39) : RGB(36, 63, 72);
    HBRUSH brush = CreateSolidBrush(background);
    RECT bounds = {0, 0, size, size};
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HFONT font = CreateFontW(-19, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(dc, font);
    std::wstring text = power.BatteryLifePercent == 255 ? L"--" : std::to_wstring(power.BatteryLifePercent);
    RECT label = {0, 1, size, 29};
    DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
    DeleteObject(font);
    if (stayAwake) {
        HBRUSH dot = CreateSolidBrush(RGB(91, 230, 146));
        RECT bar = {4, 28, 28, 31};
        FillRect(dc, &bar, dot);
        DeleteObject(dot);
    }
    GdiFlush();
    auto* raw = static_cast<DWORD*>(pixels);
    for (int i = 0; i < size * size; ++i) raw[i] |= 0xff000000;
    SelectObject(dc, old);
    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = bitmap;
    iconInfo.hbmMask = mask;
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(bitmap);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return icon;
}

void UpdateTray(bool add) {
    SYSTEM_POWER_STATUS power = {};
    if (!GetSystemPowerStatus(&power)) {
        power.BatteryLifePercent = 255;
        power.ACLineStatus = 255;
        power.BatteryLifeTime = static_cast<DWORD>(-1);
    }
    bool noBattery = (power.BatteryFlag & 128) && power.BatteryFlag != 255;
    std::wstring tip = noBattery ? L"No battery detected" : power.BatteryLifePercent == 255 ? L"Battery level unavailable" : L"Battery: " + std::to_wstring(power.BatteryLifePercent) + L"%";
    if (!noBattery) {
        if (power.ACLineStatus == 1) tip += L"\nPlugged in";
        else if (power.ACLineStatus == 0) tip += L"\n" + Duration(power.BatteryLifeTime);
        else tip += L"\nPower status unavailable";
        SYSTEM_BATTERY_STATE state = {};
        if (CallNtPowerInformation(SystemBatteryState, nullptr, 0, &state, sizeof(state)) == 0) {
            std::wstring rate = PowerRateLabel(state);
            if (!rate.empty()) tip += L"\n" + rate;
        }
    }
    tip += stayAwake ? L"\nKeep awake: on" : L"\nKeep awake: off";
    HICON icon = MakeIcon(power);
    NOTIFYICONDATAW data = {};
    data.cbSize = sizeof(data);
    data.hWnd = mainWindow;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = TrayMessage;
    data.hIcon = icon;
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    BOOL success = Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &data);
    if (currentIcon) DestroyIcon(currentIcon);
    currentIcon = icon;
    if (add && !success) {
        MessageBoxW(mainWindow, L"Battery Buddy could not add its icon to the notification area.", AppName, MB_ICONERROR);
        PostQuitMessage(1);
    }
}

void ToggleAwake() {
    bool requested = !stayAwake;
    EXECUTION_STATE state = ES_CONTINUOUS;
    if (requested) state |= ES_SYSTEM_REQUIRED;
    if (SetThreadExecutionState(state) == 0) {
        MessageBoxW(mainWindow, L"Windows could not change the sleep setting.", AppName, MB_ICONERROR);
        return;
    }
    stayAwake = requested;
    UpdateTray(false);
}

void ShowMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (stayAwake ? MF_CHECKED : 0), 1, L"Keep computer awake");
    AppendMenuW(menu, MF_STRING | (StartupEnabled() ? MF_CHECKED : 0), 2, L"Start at sign-in");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, 3, L"About");
    AppendMenuW(menu, MF_STRING, 4, L"Exit");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(mainWindow);
    int selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, mainWindow, nullptr);
    DestroyMenu(menu);
    if (selected == 1) ToggleAwake();
    if (selected == 2) SetStartup(!StartupEnabled());
    if (selected == 3) MessageBoxW(mainWindow, L"Hover over the tray icon to see battery status. Right-click it to keep the computer awake.\n\nKeep awake prevents automatic system sleep while Battery Buddy is running; the display can still turn off.", AppName, MB_OK | MB_ICONINFORMATION);
    if (selected == 4) DestroyWindow(mainWindow);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == taskbarCreated) { UpdateTray(true); return 0; }
    switch (message) {
        case WM_CREATE: mainWindow = window; SetTimer(window, RefreshTimer, 5000, nullptr); UpdateTray(true); return 0;
        case WM_TIMER: if (wParam == RefreshTimer) UpdateTray(false); return 0;
        case WM_POWERBROADCAST: UpdateTray(false); return TRUE;
        case TrayMessage: if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowMenu(); return 0;
        case WM_DESTROY: {
            KillTimer(window, RefreshTimer);
            SetThreadExecutionState(ES_CONTINUOUS);
            NOTIFYICONDATAW data = {}; data.cbSize = sizeof(data); data.hWnd = window; data.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &data);
            if (currentIcon) DestroyIcon(currentIcon);
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\BatteryBuddySingleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) { if (single) CloseHandle(single); return 0; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW klass = {};
    klass.hInstance = instance;
    klass.lpfnWndProc = WindowProc;
    klass.lpszClassName = L"BatteryBuddyWindow";
    if (!RegisterClassW(&klass)) return 1;
    mainWindow = CreateWindowExW(0, klass.lpszClassName, AppName, 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!mainWindow) return 1;
    if (commandLine && std::wstring(commandLine) == L"--keep-awake") ToggleAwake();
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (single) CloseHandle(single);
    return 0;
}


