#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// ============================================================
//  ADB helpers — obtain il2cpp base address from the Android
//  emulator's /proc/<pid>/maps via HD-Adb.exe (su shell) or
//  plain adb shell.  Reference: ShellGetAddressNoSuZ pattern.
// ============================================================

void         KillAdbZ();
std::string  AdbShell(const char* cmd);
uintptr_t    ParseBaseAddress(const std::string& mapsLine);
uintptr_t    ParseBaseAddress2(const std::string& mapsLine);
void         StartAdbInitThread();

extern std::atomic<bool> g_adbThreadRunning;
extern std::atomic<bool> g_adbInitialized;

HWND FindRenderWindow();
