#include "silentaim.h"
#include "config.h"
#include "esp.h"
#include "memory.h"
#include "offsets.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cfloat>
#include <cmath>
#include <atomic>

// ============================================================
//  SilentAim — dedicated thread
//
//  Matches the C# reference flow (KCBRUTALSILENT / SilentAimV2)
//  exactly:
//
//   while(running)
//     │ SilentAim ON?          → NO  → Sleep(1), loop
//     │ Screen ready?          → NO  → Sleep(1), loop
//     │ Scan entities          pick closest head to screen centre
//     │ Target found?          → NO  → Sleep(1), loop
//     │ Is shooting?           → NO  → Sleep(1), loop  ← ~1 kHz poll
//     │ Read AimInfo ptr
//     │ Read gun start pos
//     │ Compute ray dir = (head+0.1Y) − startPos
//     └ Write ray dir → AimInfo::RayDir (sAim4 = 0x2C)
//
//  Previous implementation ran on the 30 Hz ESP thread so it
//  missed short firing events (< 33 ms).  This thread polls at
//  ~1 kHz when idle and catches every shot.
//
//  BUG FIX (v1 / v2): isFiring was read into C++ `bool`; the
//  compiler may assume bool is only 0 or 1 and mis-optimise.
//  Now read as uint8_t — any non-zero byte means "firing".
// ============================================================

static std::atomic<bool> g_saRunning { false };
static HANDLE            g_saThread  = nullptr;

static DWORD WINAPI SilentAimProc(LPVOID)
{
    while (g_saRunning.load())
    {
        // ── Feature guard ─────────────────────────────────────────────────────
        if (!Config::SilentAimEnabled.load())
        {
            Sleep(1);
            continue;
        }

        // ── Matrix / Screen ready? ────────────────────────────────────────────
        // Uses EmulatorHwnd (set by ESP thread) to get current client size.
        HWND ew = Config::EmulatorHwnd.load();
        if (!ew) { Sleep(1); continue; }

        RECT cr{};
        GetClientRect(ew, &cr);
        const int W = cr.right, H = cr.bottom;
        if (W <= 0 || H <= 0) { Sleep(1); continue; }

        // ── Local player address ──────────────────────────────────────────────
        // Written by the ESP thread each scan cycle.
        const uint32_t localPlayer = Config::LocalPlayerAddr.load();
        if (localPlayer == 0) { Sleep(1); continue; }

        // ── Scan entities — find closest to screen centre ─────────────────────
        // Uses headScreen (emulator-relative 2D) from the ESP front buffer;
        // no FOV or distance cap, matching the reference.
        const float cx = W * 0.5f;
        const float cy = H * 0.5f;

        float   bestCross = FLT_MAX;
        Vector3 bestHead{};
        bool    found    = false;

        const bool ignKnocked = Config::AimIgnoreKnocked.load();

        {
            const EspBuffer espBuf = GetFrontBuffer();
            for (int i = 0; i < espBuf.count; ++i)
            {
                const EspEntry& e = espBuf.entries[i];
                if (!e.valid)                  continue;
                if (e.isKnocked && ignKnocked) continue;

                // headScreen is emulator-relative → compare directly to centre
                const float sx = e.headScreen.x;
                const float sy = e.headScreen.y;
                if (sx < 1.f || sy < 1.f)     continue;

                const float dx    = sx - cx;
                const float dy    = sy - cy;
                const float cross = sqrtf(dx * dx + dy * dy);

                if (cross < bestCross)
                {
                    bestCross = cross;
                    bestHead  = e.headWorld;   // world pos for ray calculation
                    found     = true;
                }
            }
        }

        if (!found) { Sleep(1); continue; }

        // ── Is the player shooting? ────────────────────────────────────────────
        // Read as uint8_t — avoids C++ bool UB (game may write 0xFF or 0x02).
        // Poll at ~1 kHz (Sleep(1)) so no shot is missed.
        uint8_t fireFlag = 0;
        if (!ReadZ(localPlayer + (uint32_t)Offsets::AimSilent1, fireFlag)
            || fireFlag == 0)
        {
            Sleep(1);
            continue;
        }

        // ── Read AimInfo pointer ──────────────────────────────────────────────
        uint32_t aimInfo = 0;
        if (!ReadZ(localPlayer + (uint32_t)Offsets::AimSilent2, aimInfo)
            || aimInfo == 0)
        {
            Sleep(0);
            continue;
        }

        // ── Read gun start position (zero if read fails — matches reference) ──
        Vector3 startPos{};
        ReadZ(aimInfo + (uint32_t)Offsets::AimSilent3, startPos);

        // ── Compute and write ray direction ───────────────────────────────────
        // Reference: adjustedTauko = target.Head + (0, 0.1f, 0)
        //            aimPosition   = adjustedTauko - startPos
        //            Write<Vector3>(aimInfo + sAim4, aimPosition)
        const Vector3 adjustedHead = { bestHead.x, bestHead.y + 0.1f, bestHead.z };
        const Vector3 rayDir       = adjustedHead - startPos;
        WriteZ<Vector3>(aimInfo + (uint32_t)Offsets::AimSilent4, rayDir);

        Sleep(0);   // yield CPU, loop immediately for next shot
    }
    return 0;
}

void StartSilentAimThread()
{
    g_saRunning.store(true);
    g_saThread = CreateThread(nullptr, 0, SilentAimProc, nullptr, 0, nullptr);
    if (g_saThread)
        SetThreadPriority(g_saThread, THREAD_PRIORITY_ABOVE_NORMAL);
}

void StopSilentAimThread()
{
    g_saRunning.store(false);
    if (g_saThread)
    {
        WaitForSingleObject(g_saThread, 2000);
        CloseHandle(g_saThread);
        g_saThread = nullptr;
    }
}
