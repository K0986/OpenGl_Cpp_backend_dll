// dllmain.cpp — DLL entry point and thread management.
//
// Thread model:
//   HookRetryThread  — retries InitHooks() every 500 ms
//   MemoryThread     — ESP data scan at ~30 Hz via thread pool
//   SilentAimThread  — dedicated 1 kHz aim thread
//   PipeServerThread — named-pipe command server
//   OverlayThread    — 60 Hz OpenGL render (inside RunOverlay)
//
// Crash safety:
//   UpdateEspData() and RunOverlay() are guarded by SEH wrappers.
//   Access violations during memory scanning no longer crash HD-Player.exe.
//
// MSVC C2712 note:
//   __try cannot appear in a function that has any C++ object unwinding
//   (std::string, std::chrono temporaries, etc.).  Every __try/__except
//   therefore lives in its own thin wrapper (SEH_*) that contains only
//   POD locals.  The real C++ work is done by the called functions.

#pragma comment(lib, "winmm.lib")

#include <atomic>
#include <chrono>
#include <thread>
#include "config.h"
#include "overlay.h"
#include "esp.h"
#include "adb.h"
#include "hooks.h"
#include "silentaim.h"
#include "pipe_server.h"
#include "thread_pool.h"

#include <Windows.h>
#include <mmsystem.h>

ThreadPool* g_threadPool = nullptr;

static std::atomic<bool> g_memThreadRun  { false };
static std::atomic<bool> g_hookThreadRun { false };

// =============================================================================
//  SEH wrappers — NO C++ objects; __try is legal here
// =============================================================================

// Returns true if InitHooks() succeeded without a structured exception.
static bool SEH_TryInitHooks()
{
    __try   { return InitHooks(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Calls UpdateEspData(w,h); silently swallows access violations.
// A stale pointer during a round transition causes an AV that would otherwise
// kill HD-Player.exe — this keeps the host alive and the next frame retries.
static void SEH_TryUpdateEsp(int w, int h)
{
    __try   { UpdateEspData(w, h); }
    __except(EXCEPTION_EXECUTE_HANDLER) { /* stale ptr — next frame retries */ }
}

// Calls RunOverlay(hInst); swallows render-loop crashes so cleanup still runs.
static void SEH_TryRunOverlay(HINSTANCE hInst)
{
    __try   { RunOverlay(hInst); }
    __except(EXCEPTION_EXECUTE_HANDLER) { /* overlay faulted — fall through */ }
}

// =============================================================================
//  HookRetryThread — retries InitHooks() every 500 ms until BstkVMM is ready
// =============================================================================
static DWORD WINAPI HookRetryThread(LPVOID)
{
    while (g_hookThreadRun.load())
    {
        if (!Config::HookReady.load())
        {
            if (SEH_TryInitHooks())
                Config::HookReady.store(true);
        }
        else break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return 0;
}

// =============================================================================
//  MemoryThread — ESP scan at ~30 Hz
// =============================================================================
static DWORD WINAPI MemoryThread(LPVOID)
{
    while (g_memThreadRun.load())
    {
        if (Config::HookReady.load() && Config::Il2CppBase.load() != 0)
        {
            HWND ew = Config::EmulatorHwnd.load();
            if (!ew) ew = FindRenderWindow();
            if (ew)
            {
                Config::EmulatorHwnd.store(ew);
                RECT cr{};
                if (GetClientRect(ew, &cr) && cr.right > 0 && cr.bottom > 0)
                    SEH_TryUpdateEsp(cr.right, cr.bottom);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    return 0;
}

// =============================================================================
//  MainThread — spawns all workers, then blocks in RunOverlay
// =============================================================================
static DWORD WINAPI MainThread(LPVOID hInstParam)
{
    HINSTANCE hInst = (HINSTANCE)hInstParam;

    timeBeginPeriod(1);

    g_threadPool = new ThreadPool();

    StartPipeServer();

    g_memThreadRun.store(true);
    g_hookThreadRun.store(true);

    HANDLE hMem  = CreateThread(nullptr, 0, MemoryThread,    nullptr, 0, nullptr);
    HANDLE hHook = CreateThread(nullptr, 0, HookRetryThread, nullptr, 0, nullptr);

    if (hMem)  SetThreadPriority(hMem,  THREAD_PRIORITY_BELOW_NORMAL);
    if (hHook) SetThreadPriority(hHook, THREAD_PRIORITY_BELOW_NORMAL);

    StartSilentAimThread();

    // RunOverlay blocks until the overlay window is closed.
    // SEH wrapper lets cleanup below always execute even on a render crash.
    SEH_TryRunOverlay(hInst);

    g_memThreadRun.store(false);
    g_hookThreadRun.store(false);

    StopSilentAimThread();
    StopPipeServer();

    if (hMem)  { WaitForSingleObject(hMem,  3000); CloseHandle(hMem);  }
    if (hHook) { WaitForSingleObject(hHook, 3000); CloseHandle(hHook); }

    delete g_threadPool;
    g_threadPool = nullptr;

    timeEndPeriod(1);
    FreeLibraryAndExitThread(hInst, 0);
    return 0;
}

// =============================================================================
//  DllMain
// =============================================================================
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hInst);
        HANDLE h = CreateThread(nullptr, 0, MainThread, hInst, 0, nullptr);
        if (h) CloseHandle(h);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        g_memThreadRun.store(false);
        g_hookThreadRun.store(false);
        ShutdownHooks();
    }
    return TRUE;
}
