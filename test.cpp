#define wWinMain BatteryBuddyMain
#include "BatteryBuddy.cpp"
#undef wWinMain
#include <cstdio>
int main() {
    SYSTEM_POWER_STATUS status = {};
    if (!GetSystemPowerStatus(&status)) return 1;
    if (status.BatteryLifePercent > 100 && status.BatteryLifePercent != 255) return 2;
    if (Duration(3660) != L"About 1 hr 1 min remaining") return 3;
    if (Duration(static_cast<DWORD>(-1)) != L"Time remaining unavailable") return 4;
    SYSTEM_BATTERY_STATE battery = {};
    battery.BatteryPresent = TRUE;
    battery.Discharging = TRUE;
    battery.Rate = static_cast<DWORD>(-27600L);
    if (PowerRateLabel(battery) != L"Discharging: 27.6 W") return 7;
    battery.Rate = static_cast<DWORD>(LONG_MIN);
    if (!PowerRateLabel(battery).empty()) return 8;
    battery.Discharging = FALSE;
    battery.Charging = TRUE;
    battery.Rate = 12500;
    if (PowerRateLabel(battery) != L"Charging: 12.5 W") return 9;
    SYSTEM_BATTERY_STATE live = {};
    CallNtPowerInformation(SystemBatteryState, nullptr, 0, &live, sizeof(live));
    std::wprintf(L"Live battery: %ls\n", PowerRateLabel(live).c_str());
    ToggleAwake();
    if (!stayAwake) return 5;
    ToggleAwake();
    if (stayAwake) return 6;
    std::printf("Power status read; duration formatting and awake on/off passed.\n");
    return 0;
}


