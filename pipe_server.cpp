// pipe_server.cpp — Named pipe server  \\.\pipe\esp_pipe
//
// Protocol: newline-terminated UTF-8 commands (client → server).
//
// Startup sequence:
//   Client sends:   start
//   Server replies: command received please wait
//   Then setup.cpp background thread sends:
//       step 1 passed   OR   step 1 failed: <reason>
//       step 2 passed   OR   step 2 failed: <reason>
//       step 3 passed   OR   step 3 failed: <reason>
//
// After setup completes, the ONLY output is feature command replies.
// No unsolicited messages are ever sent. PipeLog() is a no-op (see pipe_server.h).
//
// Feature reply format:  "<Feature Name> on"  or  "<Feature Name> off"
//   e.g.  box:on  →  "Box ESP on"
//         skel:off →  "Skeleton off"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>

#include "pipe_server.h"
#include "config.h"
#include "esp.h"
#include "adb.h"
#include "setup.h"
#include "overlay.h"

#include <cstdio>
#include <cstring>
#include <cctype>
#include <atomic>
#include <string>

// ── Pipe name ─────────────────────────────────────────────────────────────────
static const wchar_t* PIPE_NAME = L"\\\\.\\pipe\\esp_pipe";

// ── Thread control ────────────────────────────────────────────────────────────
static std::atomic<bool> g_pipeRun    { false };
static HANDLE            g_pipeThread = nullptr;

// ── Critical sections ─────────────────────────────────────────────────────────
// g_writeCs — guards ALL WriteFile calls on the active client handle.
//             setup.cpp calls PipeWrite() from its background thread while
//             the pipe-server thread may also reply to a command — the lock
//             ensures the bytes never interleave.
// g_clientCs — guards g_clientHandle itself (read/write of the HANDLE value).
static CRITICAL_SECTION g_writeCs;
static CRITICAL_SECTION g_clientCs;
static HANDLE           g_clientHandle = INVALID_HANDLE_VALUE;

// =============================================================================
//  PipeWrite — thread-safe pipe write (exported for setup.cpp)
// =============================================================================
void PipeWrite(HANDLE h, const char* msg)
{
    if (!h || h == INVALID_HANDLE_VALUE || !msg || !*msg) return;
    DWORD w = 0;
    EnterCriticalSection(&g_writeCs);
    WriteFile(h, msg, (DWORD)strlen(msg), &w, nullptr);
    WriteFile(h, "\n", 1, &w, nullptr);
    LeaveCriticalSection(&g_writeCs);
}

// Internal aliases — everything goes through PipeWrite so the lock is held
static void SendLine(HANDLE h, const char* msg) { PipeWrite(h, msg); }
static void SendOk  (HANDLE h) { SendLine(h, "ok");   }
static void SendPong(HANDLE h) { SendLine(h, "pong"); }

// =============================================================================
//  Status reply
// =============================================================================
static void SendStatus(HANDLE h)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
        "status "
        "adb=%d hook=%d il2cpp=%d conv=%d facade=%d entities=%u "
        "lines=%d box=%d skel=%d name=%d weapon=%d health=%d "
        "aimbot=%d silent=%d aimvis=%d fov=%d norecoil=%d stats=%d",
        (int)Config::AdbReady.load(),
        (int)Config::HookReady.load(),
        (int)(Config::Il2CppBase.load() != 0),
        (int)Config::StageConvert.load(),
        (int)Config::StageGameFacade.load(),
        (unsigned)Config::EspEntityCount.load(),
        (int)Config::EspLinesEnabled.load(),
        (int)Config::EspBoxEnabled.load(),
        (int)Config::EspSkeletonEnabled.load(),
        (int)Config::EspNameEnabled.load(),
        (int)Config::EspWeaponEnabled.load(),
        (int)Config::EspHealthEnabled.load(),
        (int)Config::AimbotEnabled.load(),
        (int)Config::SilentAimEnabled.load(),
        (int)Config::AimVisibleEnabled.load(),
        (int)Config::AimFovEnabled.load(),
        (int)Config::NoRecoilEnabled.load(),
        (int)Config::StatsOverlayEnabled.load());
    SendLine(h, buf);
}

// =============================================================================
//  Parsing helpers
// =============================================================================
static bool StartsWith(const char* s, const char* prefix)
{
    while (*prefix)
    {
        if (!*s || tolower((unsigned char)*s) != tolower((unsigned char)*prefix))
            return false;
        ++s; ++prefix;
    }
    return true;
}

static bool ParseBool(const char* val)
{
    return strcmp(val, "on")   == 0 || strcmp(val, "1")    == 0 ||
           strcmp(val, "true") == 0 || strcmp(val, "yes")  == 0;
}

static bool ParseRGB(const char* s, BYTE& r, BYTE& g, BYTE& b)
{
    int ri = 0, gi = 0, bi = 0;
    if (sscanf_s(s, "%d:%d:%d", &ri, &gi, &bi) != 3) return false;
    r = (BYTE)ri; g = (BYTE)gi; b = (BYTE)bi;
    return true;
}

// =============================================================================
//  Command dispatcher
// =============================================================================
static bool DispatchCommand(HANDLE h, char* line)
{
    // Trim trailing whitespace / CR LF
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n' || line[len-1] == ' '))
        line[--len] = '\0';
    if (len == 0) return true;

    // Lowercase the command word only (up to first ':' or space)
    for (size_t i = 0; i < len; ++i)
    {
        if (line[i] == ':' || line[i] == ' ') break;
        line[i] = (char)tolower((unsigned char)line[i]);
    }

    // ── start — 3-step guided setup ──────────────────────────────────────────
    // Immediately acknowledge, then setup.cpp background thread does the work
    // and sends "step N passed / failed" messages via PipeWrite().
    if (strcmp(line, "start") == 0)
    {
        if (Setup_IsRunning())
        {
            SendLine(h, "err Setup is already running — wait for step 3 to complete");
            return true;
        }
        SendLine(h, "command received please wait");
        Setup_Start(h);
        return true;
    }

    // ── ping ─────────────────────────────────────────────────────────────────
    if (strcmp(line, "ping") == 0) { SendPong(h); return true; }

    // ── status ───────────────────────────────────────────────────────────────
    if (strcmp(line, "status") == 0) { SendStatus(h); return true; }

    // ── adb_status — compact one-line poll for panel indicators ──────────────
    if (strcmp(line, "adb_status") == 0)
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "adb_status adb=%d hook=%d facade=%d entities=%u",
            (int)Config::AdbReady.load(),
            (int)Config::HookReady.load(),
            (int)Config::StageGameFacade.load(),
            (unsigned)Config::EspEntityCount.load());
        SendLine(h, buf);
        return true;
    }

    // ── quit / exit ──────────────────────────────────────────────────────────
    if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0)
    {
        SendLine(h, "bye");
        return false;   // signals HandleClient to disconnect
    }

    // ── connect_adb — standalone trigger (bypasses setup flow) ───────────────
    if (strcmp(line, "connect_adb") == 0)
    {
        StartAdbInitThread();
        SendOk(h);
        return true;
    }

    // ── reset_esp / autorefresh ───────────────────────────────────────────────
    if (strcmp(line, "reset_esp") == 0 || strcmp(line, "autorefresh") == 0)
    {
        Config::ResetEspRequested.store(true);
        SendOk(h);
        return true;
    }

    // ── clear_cache / clearcache ──────────────────────────────────────────────
    if (strcmp(line, "clear_cache") == 0 || strcmp(line, "clearcache") == 0)
    {
        Config::ClearCacheRequested.store(true);
        SendOk(h);
        return true;
    }

    // ── esp:on / esp:off — bulk toggle all visual ESP features ───────────────
    if (StartsWith(line, "esp:"))
    {
        bool v = ParseBool(line + 4);
        Config::EspLinesEnabled.store(v);
        Config::EspBoxEnabled.store(v);
        Config::EspSkeletonEnabled.store(v);
        Config::EspNameEnabled.store(v);
        Config::EspHealthEnabled.store(v);
        char buf[32];
        snprintf(buf, sizeof(buf), "All ESP %s", v ? "on" : "off");
        SendLine(h, buf);
        return true;
    }

    // ── Individual feature toggles ────────────────────────────────────────────
    // Reply: "<Label> on"  or  "<Label> off"
    // This is the primary output after setup — every feature click returns exactly this.
    struct { const char* prefix; std::atomic<bool>* flag; const char* label; } features[] =
    {
        { "lines:",       &Config::EspLinesEnabled,    "Snap Lines"    },
        { "box:",         &Config::EspBoxEnabled,      "Box ESP"       },
        { "skel:",        &Config::EspSkeletonEnabled, "Skeleton"      },
        { "name:",        &Config::EspNameEnabled,     "Player Names"  },
        { "weapon:",      &Config::EspWeaponEnabled,   "Weapon Label"  },
        { "health:",      &Config::EspHealthEnabled,   "Health Bar"    },
        { "stats:",       &Config::StatsOverlayEnabled,"Stats HUD"     },
        { "aimbot:",      &Config::AimbotEnabled,      "Aimbot"        },
        { "silentaim:",   &Config::SilentAimEnabled,   "Silent Aim"    },
        { "aimvisible:",  &Config::AimVisibleEnabled,  "Aim Visible"   },
        { "fov:",         &Config::AimFovEnabled,      "FOV Circle"    },
        { "norecoil:",    &Config::NoRecoilEnabled,    "No Recoil"     },
        { "showvisible:", &Config::ShowVisibleOnly,    "Visible Only"  },
        { "knockedtint:", &Config::ShowKnockedTint,    "Knocked Tint"  },
        { "gradient:",    &Config::GradientColor,      "Gradient"      },
        { "smoothlines:", &Config::SmoothLines,        "Smooth Lines"  },
        { "ignorekd:",    &Config::AimIgnoreKnocked,   "Ignore Knocked"},
    };

    for (auto& f : features)
    {
        if (StartsWith(line, f.prefix))
        {
            bool v = ParseBool(line + strlen(f.prefix));
            f.flag->store(v);
            char buf[64];
            snprintf(buf, sizeof(buf), "%s %s", f.label, v ? "on" : "off");
            SendLine(h, buf);
            return true;
        }
    }

    // ── Numeric parameters ────────────────────────────────────────────────────
    if (StartsWith(line, "aimfov:"))
    {
        float v = (float)atof(line + 7);
        if (v >= 10.f && v <= 1000.f) { Config::AimFov.store(v); SendOk(h); }
        else SendLine(h, "err FOV radius must be between 10 and 1000");
        return true;
    }

    if (StartsWith(line, "aimdist:"))
    {
        float v = (float)atof(line + 8);
        if (v >= 1.f) { Config::AimMaxDistance.store(v); SendOk(h); }
        else SendLine(h, "err Aim distance must be at least 1 metre");
        return true;
    }

    if (StartsWith(line, "maxdist:"))
    {
        int v = atoi(line + 8);
        if (v >= 1) { Config::MaxDistance.store(v); SendOk(h); }
        else SendLine(h, "err Max distance must be at least 1");
        return true;
    }

    if (StartsWith(line, "aimtarget:"))
    {
        int v = atoi(line + 10);
        if (v >= 0 && v <= 2)
        {
            Config::AimTargetPart.store(v);
            const char* names[] = { "Head", "Neck", "Hip" };
            char buf[32];
            snprintf(buf, sizeof(buf), "Aim bone: %s", names[v]);
            SendLine(h, buf);
        }
        else SendLine(h, "err Aim target: 0=Head 1=Neck 2=Hip");
        return true;
    }

    if (StartsWith(line, "linethick:"))
    {
        float v = (float)atof(line + 10);
        if (v >= 0.5f && v <= 8.f) { Config::LineThickness.store(v); SendOk(h); }
        else SendLine(h, "err Line thickness must be 0.5 to 8.0");
        return true;
    }

    if (StartsWith(line, "skelthick:"))
    {
        float v = (float)atof(line + 10);
        if (v >= 0.5f && v <= 8.f) { Config::SkeletonThickness.store(v); SendOk(h); }
        else SendLine(h, "err Skeleton thickness must be 0.5 to 8.0");
        return true;
    }

    // ── Colour commands: color:<target>:<R>:<G>:<B> ───────────────────────────
    if (StartsWith(line, "color:"))
    {
        const char* rest = line + 6;
        BYTE r = 0, g = 0, b = 0;
        bool ok = false;

        if      (StartsWith(rest, "lines:"))  { if (ParseRGB(rest+6, r,g,b)) { Config::LineColor.store(RGB(r,g,b));       ok=true; } }
        else if (StartsWith(rest, "box:"))    { if (ParseRGB(rest+4, r,g,b)) { Config::BoxColor.store(RGB(r,g,b));        ok=true; } }
        else if (StartsWith(rest, "skel:"))   { if (ParseRGB(rest+5, r,g,b)) { Config::SkelColor.store(RGB(r,g,b));       ok=true; } }
        else if (StartsWith(rest, "weapon:")) { if (ParseRGB(rest+7, r,g,b)) { Config::WeaponNameColor.store(RGB(r,g,b)); ok=true; } }
        else if (StartsWith(rest, "fov:"))    { if (ParseRGB(rest+4, r,g,b)) { Config::AimFovColor.store(RGB(r,g,b));    ok=true; } }

        if (ok) SendOk(h);
        else    SendLine(h, "err Format: color:<target>:<R>:<G>:<B>   targets: lines box skel weapon fov");
        return true;
    }

    // ── Unknown command ───────────────────────────────────────────────────────
    char errBuf[160];
    snprintf(errBuf, sizeof(errBuf),
        "err Unknown command '%.60s' — send 'ping' to test or 'status' for diagnostics",
        line);
    SendLine(h, errBuf);
    return true;
}

// =============================================================================
//  Client session handler
// =============================================================================
static void HandleClient(HANDLE hPipe)
{
    // Register the active client handle so PipeWrite can reach it
    EnterCriticalSection(&g_clientCs);
    g_clientHandle = hPipe;
    LeaveCriticalSection(&g_clientCs);

    char raw[512];
    DWORD nRead = 0;
    std::string partial;

    while (g_pipeRun.load())
    {
        BOOL ok = ReadFile(hPipe, raw, sizeof(raw) - 1, &nRead, nullptr);
        if (!ok || nRead == 0) break;

        raw[nRead] = '\0';
        partial += raw;

        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos)
        {
            std::string cmd = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            if (!cmd.empty() && cmd.back() == '\r') cmd.pop_back();
            if (!cmd.empty())
            {
                char cb[512];
                strncpy_s(cb, cmd.c_str(), _TRUNCATE);
                if (!DispatchCommand(hPipe, cb)) goto disconnect;
            }
        }
    }

disconnect:
    EnterCriticalSection(&g_clientCs);
    g_clientHandle = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&g_clientCs);

    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
}

// =============================================================================
//  Pipe server thread
// =============================================================================
static DWORD WINAPI PipeServerThread(LPVOID)
{
    while (g_pipeRun.load())
    {
        HANDLE hPipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);

        if (hPipe == INVALID_HANDLE_VALUE) { Sleep(500); continue; }

        BOOL connected = ConnectNamedPipe(hPipe, nullptr)
                         ? TRUE
                         : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);

        if (connected && g_pipeRun.load())
            HandleClient(hPipe);
        else
        {
            CloseHandle(hPipe);
            Sleep(50);
        }
    }
    return 0;
}

// =============================================================================
//  Public API
// =============================================================================
void StartPipeServer()
{
    InitializeCriticalSection(&g_writeCs);
    InitializeCriticalSection(&g_clientCs);
    g_pipeRun.store(true);
    g_pipeThread = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
    if (g_pipeThread)
        SetThreadPriority(g_pipeThread, THREAD_PRIORITY_NORMAL);
}

void StopPipeServer()
{
    g_pipeRun.store(false);

    // Unblock ConnectNamedPipe by opening a dummy connection
    HANDLE dummy = CreateFileW(PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE) CloseHandle(dummy);

    if (g_pipeThread)
    {
        WaitForSingleObject(g_pipeThread, 3000);
        CloseHandle(g_pipeThread);
        g_pipeThread = nullptr;
    }

    DeleteCriticalSection(&g_writeCs);
    DeleteCriticalSection(&g_clientCs);
}
