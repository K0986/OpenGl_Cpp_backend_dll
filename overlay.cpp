// overlay.cpp — Full-screen transparent OpenGL overlay.
//
// Responsibilities:
//   - Create/destroy the layered click-through window
//   - Manage the OpenGL context and render thread
//   - Run a waitable-timer render loop at exactly 60 Hz
//   - Delegate all drawing to gl_draw.h / esp_render.h
//   - Expose UiSetStatus() for status updates (forwards to PipeLog)
//
// Does NOT do any direct GL drawing — all primitives live in gl_draw.cpp
// and all entity rendering lives in esp_render.cpp.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <GL/gl.h>

#include "config.h"
#include "overlay.h"
#include "pipe_server.h"
#include "esp.h"
#include "adb.h"
#include "hooks.h"
#include "gl_draw.h"
#include "esp_render.h"

#include <cstdio>
#include <cstring>
#include <atomic>

// ── GL / window state ─────────────────────────────────────────────────────────
static HWND  g_overlay   = nullptr;
static HDC   g_overlayDC = nullptr;
static HGLRC g_overlayRC = nullptr;
static int   g_scrW = 0, g_scrH = 0;

// ── Render thread ─────────────────────────────────────────────────────────────
static std::atomic<bool> g_overlayRun { false };
static HANDLE            g_overlayThread = nullptr;

// ── Status strip (written by any thread, read by render thread) ───────────────
static char             s_statusMain[128] = "Pipe ESP";
static char             s_statusStep[256] = "Waiting...";
static CRITICAL_SECTION s_statusCs;

// ── Emulator window mapping ───────────────────────────────────────────────────
static void GetEmulatorMapping(int& eLeft, int& eTop, int& eW, int& eH)
{
    eLeft = 0; eTop = 0; eW = g_scrW; eH = g_scrH;

    HWND ew = Config::EmulatorHwnd.load();
    if (!ew || !IsWindow(ew)) {
        ew = FindRenderWindow();
        if (ew) Config::EmulatorHwnd.store(ew);
        if (!ew) return;
    }
    RECT cr{};
    if (!GetClientRect(ew, &cr) || cr.right <= 0 || cr.bottom <= 0) return;
    POINT origin{0, 0};
    ClientToScreen(ew, &origin);
    eLeft = origin.x;
    eTop  = origin.y;
    eW    = (int)cr.right;
    eH    = (int)cr.bottom;
}

// ── Render one frame ──────────────────────────────────────────────────────────
static void RenderFrame()
{
    glClear(GL_COLOR_BUFFER_BIT);

    int eLeft = 0, eTop = 0, eW = g_scrW, eH = g_scrH;
    GetEmulatorMapping(eLeft, eTop, eW, eH);

    // All entity drawing + FOV + pink HUD bar
    EspRender_Draw(eLeft, eTop, eW, eH);

    SwapBuffers(g_overlayDC);
}

// ── Render thread — 60 Hz using a high-resolution waitable timer ──────────────
static DWORD WINAPI OverlayThreadProc(LPVOID)
{
    wglMakeCurrent(g_overlayDC, g_overlayRC);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    GL_Ortho2D(g_scrW, g_scrH);

    EspRender_Init(g_overlayDC);

    // High-resolution periodic waitable timer at 60 Hz (16.6667 ms)
    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION requires Windows 10 1803+;
    // fall back to regular timer if unavailable.
    HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr,
                        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!hTimer)
        hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);

    LARGE_INTEGER due;
    due.QuadPart = -333333LL;   // 33.3333 ms in 100-ns units (first fire)
    const LONG periodMs = 33;   // ~30 Hz repeat
    if (hTimer)
        SetWaitableTimer(hTimer, &due, periodMs, nullptr, nullptr, FALSE);

    while (g_overlayRun.load())
    {
        if (hTimer)
            WaitForSingleObject(hTimer, 40);   // wait ≤ 40 ms in case timer misfires
        else
            Sleep(33);

        RenderFrame();
    }

    if (hTimer) { CancelWaitableTimer(hTimer); CloseHandle(hTimer); }

    EspRender_Shutdown();
    wglMakeCurrent(nullptr, nullptr);
    return 0;
}

// ── Window procedure ──────────────────────────────────────────────────────────
static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ── RunOverlay — creates the overlay window and blocks on the message loop ────
int RunOverlay(HINSTANCE hInst)
{
    InitializeCriticalSection(&s_statusCs);

    g_scrW = GetSystemMetrics(SM_CXSCREEN);
    g_scrH = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wco{};
    wco.cbSize        = sizeof(wco);
    wco.lpfnWndProc   = OverlayProc;
    wco.hInstance     = hInst;
    wco.lpszClassName = L"FFEspOverlayGL";
    RegisterClassExW(&wco);

    g_overlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        L"FFEspOverlayGL", L"", WS_POPUP,
        0, 0, g_scrW, g_scrH, nullptr, nullptr, hInst, nullptr);

    SetLayeredWindowAttributes(g_overlay, RGB(0,0,0), 0, LWA_COLORKEY);
    ShowWindow(g_overlay, SW_SHOW);

    g_overlayDC = GetDC(g_overlay);

    PIXELFORMATDESCRIPTOR pfd{};
    GL_FillPFD(pfd, true);
    int pf = ChoosePixelFormat(g_overlayDC, &pfd);
    SetPixelFormat(g_overlayDC, pf, &pfd);
    g_overlayRC = wglCreateContext(g_overlayDC);

    g_overlayRun.store(true);
    g_overlayThread = CreateThread(nullptr, 0, OverlayThreadProc, nullptr, 0, nullptr);
    if (g_overlayThread)
        SetThreadPriority(g_overlayThread, THREAD_PRIORITY_NORMAL);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_overlayRun.store(false);
    if (g_overlayThread) {
        WaitForSingleObject(g_overlayThread, 3000);
        CloseHandle(g_overlayThread);
        g_overlayThread = nullptr;
    }

    wglDeleteContext(g_overlayRC);
    ReleaseDC(g_overlay, g_overlayDC);
    DeleteCriticalSection(&s_statusCs);
    return (int)msg.wParam;
}

// ── PostRedraw — kept for API compatibility (no longer drives rendering) ──────
void PostRedraw()
{
    // Render loop is timer-driven; this is a no-op kept for callers.
}

// ── UiSetStatus — update the internal status strings (overlay HUD only) ───────
// Does NOT write to the pipe. All pipe output is controlled exclusively by
// setup.cpp (step messages) and pipe_server.cpp (command replies).
void UiSetStatus(const char* mainLine, const char* stepLine)
{
    EnterCriticalSection(&s_statusCs);
    if (mainLine) strncpy_s(s_statusMain, mainLine, _TRUNCATE);
    if (stepLine) strncpy_s(s_statusStep, stepLine,  _TRUNCATE);
    LeaveCriticalSection(&s_statusCs);
}
