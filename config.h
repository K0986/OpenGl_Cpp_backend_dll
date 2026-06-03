#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <atomic>
#include <cstdint>
#include <string>
#include <mutex>
#include <Windows.h>

namespace Config
{
    // ── Feature toggles ─────────────────────────────────────
    inline std::atomic<bool> EspLinesEnabled    { false };
    inline std::atomic<bool> EspBoxEnabled      { false };
    inline std::atomic<bool> EspSkeletonEnabled { false };
    inline std::atomic<bool> EspNameEnabled     { false };
    inline std::atomic<bool> EspWeaponEnabled   { false };
    inline std::atomic<bool> AdbConnected       { false };

    inline std::atomic<bool> EspHealthEnabled   { false };
    inline std::atomic<bool> ShowVisibleOnly    { false };

    // ── Rendering options ────────────────────────────────────
    inline std::atomic<bool> SmoothLines        { true };
    inline std::atomic<bool> GradientColor      { true };
    inline std::atomic<bool> ShowKnockedTint    { true };

    // ── ADB / memory state ───────────────────────────────────
    inline std::atomic<uintptr_t> Il2CppBase  { 0 };
    inline std::atomic<bool>      AdbReady    { false };
    inline std::atomic<bool>      HookReady   { false };
    inline std::atomic<bool>      AdbBusy     { false };

    // ── ESP pipeline stage flags ─────────────────────────────
    inline std::atomic<bool>      StageConvert     { false };
    inline std::atomic<bool>      StageIl2Cpp      { false };
    inline std::atomic<bool>      StageGameFacade  { false };
    inline std::atomic<bool>      StageCurrentGame { false };
    inline std::atomic<bool>      StageMatch       { false };
    inline std::atomic<bool>      StageLocalPlayer { false };
    inline std::atomic<bool>      StageViewMatrix  { false };
    inline std::atomic<bool>      StageEntities    { false };
    inline std::atomic<uint32_t>  EspEntityCount   { 0 };
    inline std::atomic<uint32_t>  EspDrawnCount    { 0 };
    inline std::atomic<bool>      LocalPlayerDead      { false };
    inline std::atomic<bool>      ClearCacheRequested  { false };
    inline std::atomic<bool>      ResetEspRequested    { false };

    // ── Status log ──────────────────────────────────────────
    inline char EspLastError[256] = "Not scanned yet";
    inline std::mutex EspLastErrorMtx;

    inline void SetEspLastError(const char* msg) {
        std::lock_guard<std::mutex> lk(EspLastErrorMtx);
        strncpy_s(EspLastError, msg, _TRUNCATE);
    }
    inline std::string GetEspLastError() {
        std::lock_guard<std::mutex> lk(EspLastErrorMtx);
        return std::string(EspLastError);
    }

    // ── Emulator window ──────────────────────────────────────
    inline std::atomic<HWND> EmulatorHwnd { nullptr };

    // ── Drawing parameters ───────────────────────────────────
    inline std::atomic<int>   MaxDistance         { 9999 };
    inline std::atomic<float> LineThickness       { 2.5f };
    inline std::atomic<float> SkeletonThickness   { 2.0f };

    // ── ESP colours  (COLORREF: 0x00BBGGRR) ─────────────────
    inline std::atomic<COLORREF> LineColor       { RGB(0, 255, 0)    };
    inline std::atomic<COLORREF> BoxColor        { RGB(255, 165, 0)  };
    inline std::atomic<COLORREF> SkelColor       { RGB(0, 220, 255)  };
    inline std::atomic<COLORREF> KnockedColor    { RGB(255, 255, 0)  };
    inline std::atomic<COLORREF> WeaponNameColor { RGB(0, 255, 255)  };

    inline std::atomic<float>  GradNear { 50.f };
    inline std::atomic<float>  GradFar  { 300.f };

    // ── Game package ─────────────────────────────────────────
    inline const std::string GamePackage = "com.dts.freefireth";

    // ── Aimbot ───────────────────────────────────────────────
    inline std::atomic<bool>  AimbotEnabled    { false };
    inline std::atomic<bool>  AimFovEnabled    { false };
    inline std::atomic<float> AimFov           { 200.f };
    inline std::atomic<float> AimMaxDistance   { 200.f };
    inline std::atomic<int>   AimTargetPart    { 0 };
    inline std::atomic<bool>  AimIgnoreKnocked { false };

    // ── SilentAim ────────────────────────────────────────────
    inline std::atomic<bool>     SilentAimEnabled { false };
    inline std::atomic<uint32_t> LocalPlayerAddr  { 0 };

    // ── AimVisible ───────────────────────────────────────────
    inline std::atomic<bool>  AimVisibleEnabled { false };

    // ── NoRecoil ─────────────────────────────────────────────
    inline std::atomic<bool>  NoRecoilEnabled  { false };

    // ── Stats overlay ────────────────────────────────────────
    inline std::atomic<bool> StatsOverlayEnabled { true };

    // ── Aim FOV colour ───────────────────────────────────────
    inline std::atomic<COLORREF> AimFovColor { RGB(0, 255, 255) };

    // ── Box geometry ─────────────────────────────────────────
    constexpr float BoxWidthRatio = 0.38f;

    // ── Health bar geometry ───────────────────────────────────
    constexpr float HealthBarW   = 6.f;
    constexpr float HealthBarGap = 3.f;
    inline std::atomic<COLORREF> HealthColorHi { RGB(0, 220, 80)  };
    inline std::atomic<COLORREF> HealthColorLo { RGB(255, 50, 50) };

    // ── Menu hotkey (kept for compatibility) ──────────────────
    inline std::atomic<int> MenuHotkey { VK_INSERT };
}
