// setup.cpp — 3-step guided setup flow.
  //
  // Protocol (exactly what the pipe client receives):
  //
  //   Client sends:   start
  //   Server sends:   command received please wait
  //
  //   Step 1 — Memory hooks + ADB root + il2cpp base found
  //     Success:  [STEP 1] passed
  //     Failure:  [STEP 1] failed: <plain-English reason>
  //
  //   Step 2 — Memory translation (ConvertZ) working
  //     Success:  [STEP 2] passed
  //     Failure:  [STEP 2] failed: <plain-English reason>
  //
  //   Step 3 — Game pointer chain resolves
  //     Success:  [STEP 3] passed
  //     Failure:  [STEP 3] failed: <plain-English reason>
  //
  // Re-run safety:
  //   Stage flags from any previous run are cleared before each new run so
  //   that "step N passed" from a prior successful setup never carries over
  //   and short-circuits a re-run triggered after a crash or reconnect.
  //
  // Cancellation:
  //   Setup_Cancel() sets s_cancelRequested which WaitFor() checks on every
  //   poll interval.  This lets Setup_Start() forcibly abort a stale run
  //   from a previous HD-Player.exe session before starting a fresh one,
  //   instead of returning "setup already running" to the client.
  //
  // Thread safety:
  //   All pipe writes go through PipeWrite() (pipe_server.cpp) which holds
  //   the shared write critical section, so step messages never interleave
  //   with command replies that arrive concurrently.

  #ifndef WIN32_LEAN_AND_MEAN
  #  define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>

  #include "setup.h"
  #include "config.h"
  #include "adb.h"
  #include "pipe_server.h"

  #include <atomic>

  static std::atomic<bool>  s_running         { false };
  static std::atomic<bool>  s_cancelRequested { false };

  // Timestamp (GetTickCount64) when the current setup run started.
  // Used to detect runs that have been alive impossibly long (stale DLL).
  static std::atomic<ULONGLONG> s_runStartTick { 0 };

  // Maximum time a setup run is considered valid before it can be force-
  // cancelled by a subsequent Setup_Start() call.  150 s covers the full
  // worst-case hook + ADB (120 s) + step 3 (60 s) budget with margin.
  static constexpr ULONGLONG kMaxRunAge = 150000ULL;

  static inline void Send(HANDLE h, const char* msg) { PipeWrite(h, msg); }

  // ── WaitFor ──────────────────────────────────────────────────────────────────
  // Polls cond() every pollMs until it returns true or timeoutMs elapses.
  // Also returns false immediately if s_cancelRequested is set, which lets
  // a new Setup_Start() abort a stale run that is stuck in a WaitFor loop.
  template<typename Cond>
  static bool WaitFor(Cond cond, int timeoutMs, int pollMs = 350)
  {
      for (int elapsed = 0; elapsed < timeoutMs; elapsed += pollMs)
      {
          if (s_cancelRequested.load()) return false;
          if (cond()) return true;
          Sleep(pollMs);
      }
      if (s_cancelRequested.load()) return false;
      return cond();
  }

  // =============================================================================
  //  ResetStageFlags — clear all per-run pipeline state so that a re-run
  //  starts clean even if the previous run succeeded.
  //
  //  HookReady is intentionally NOT reset — hooks survive re-runs and
  //  re-hooking BstkVMM.dll after a clean install would require MinHook
  //  re-init which is unsafe mid-session.
  // =============================================================================
  static void ResetStageFlags()
  {
      Config::AdbReady.store(false);
      Config::Il2CppBase.store(0);
      Config::StageConvert.store(false);
      Config::StageIl2Cpp.store(false);
      Config::StageGameFacade.store(false);
      Config::StageCurrentGame.store(false);
      Config::StageMatch.store(false);
      Config::StageLocalPlayer.store(false);
      Config::StageViewMatrix.store(false);
      Config::StageEntities.store(false);
      Config::EspEntityCount.store(0);
      g_adbInitialized.store(false);
      // Note: do NOT reset g_adbThreadRunning here — the previous
      // ADB thread may not have exited yet; StartAdbInitThread() guards
      // against double-start via compare_exchange_strong.
  }

  // =============================================================================
  //  Setup thread
  // =============================================================================
  struct SetupCtx { HANDLE hPipe; };

  static DWORD WINAPI SetupThread(LPVOID lpParam)
  {
      auto* ctx = reinterpret_cast<SetupCtx*>(lpParam);
      HANDLE h  = ctx->hPipe;
      delete ctx;

      // Clear stale flags from any previous run BEFORE sending any messages
      ResetStageFlags();

      // Brief pause so the client finishes reading "command received please wait"
      Sleep(200);

      // =========================================================================
      //  STEP 1 — Memory hooks + ADB + il2cpp base
      // =========================================================================
      StartAdbInitThread();

      bool hookOk = WaitFor([]{ return Config::HookReady.load(); }, 45000);
      if (!hookOk)
      {
          // Distinguish cancellation (new run took over) from real timeout
          if (!s_cancelRequested.load())
          {
              Send(h,
                  "[STEP 1] failed: Emulator not supported — "
                  "right-click the injector and choose Run as Administrator, "
                  "then restart BlueStacks and try again");
          }
          s_running.store(false);
          return 0;
      }

      bool adbOk = WaitFor([]{ return Config::AdbReady.load(); }, 120000);
      if (!adbOk)
      {
          if (!s_cancelRequested.load())
          {
              Send(h,
                  "[STEP 1] failed: ADB connection failed — "
                  "if you just restarted BlueStacks, wait 10 seconds and try again; "
                  "otherwise open BlueStacks Settings > Advanced, "
                  "enable Android Debug Bridge, then try again");
          }
          s_running.store(false);
          return 0;
      }

      bool baseOk = WaitFor([]{ return Config::Il2CppBase.load() != 0; }, 10000);
      if (!baseOk)
      {
          if (!s_cancelRequested.load())
          {
              Send(h,
                  "[STEP 1] failed: Free Fire process not found — "
                  "make sure Free Fire is installed in BlueStacks "
                  "and has been launched at least once");
          }
          s_running.store(false);
          return 0;
      }

      Send(h, "[STEP 1] passed");

      // =========================================================================
      //  STEP 2 — Memory translation (ConvertZ) verified
      // =========================================================================
      bool convOk = WaitFor([]{ return Config::StageConvert.load(); }, 25000);
      if (!convOk)
      {
          if (!s_cancelRequested.load())
          {
              Send(h,
                  "[STEP 2] failed: Memory read failed — "
                  "restart BlueStacks, re-inject the DLL, and try again");
          }
          s_running.store(false);
          return 0;
      }

      Send(h, "[STEP 2] passed");

      // =========================================================================
      //  STEP 3 — Game pointer chain resolves
      // =========================================================================
      bool gameOk = WaitFor([]{ return Config::StageGameFacade.load(); }, 60000, 500);
      if (!gameOk)
      {
          if (!s_cancelRequested.load())
          {
              Send(h,
                  "[STEP 3] failed: Free Fire main menu not reached — "
                  "open Free Fire inside BlueStacks and wait for "
                  "the home screen to fully load, then try again");
          }
          s_running.store(false);
          return 0;
      }

      Config::ResetEspRequested.store(true);
      Send(h, "[STEP 3] passed");

      s_running.store(false);
      return 0;
  }

  // =============================================================================
  //  Public API
  // =============================================================================

  // Setup_Cancel — signal the running setup thread to abort.
  // Called internally by Setup_Start() when a fresh run needs to replace
  // a stale one.  The thread's WaitFor() loops check s_cancelRequested and
  // exit without sending an error message if it is set.
  void Setup_Cancel()
  {
      s_cancelRequested.store(true);

      // Wait up to 2 s for the thread to notice and clear s_running.
      for (int i = 0; i < 200 && s_running.load(); ++i)
          Sleep(10);

      // Force-clear in case the thread is stuck (e.g. deep in KillAdbZ).
      s_running.store(false);
      s_cancelRequested.store(false);
  }

  void Setup_Start(HANDLE hPipe)
  {
      // ── Stale-run guard ──────────────────────────────────────────────────────
      // If a previous setup run is marked "running" but was started an
      // unreasonably long time ago, it belongs to a prior HD-Player.exe
      // session that never cleaned up (e.g. the process was killed, the DLL
      // was unloaded mid-run, and the same DLL instance is now being reused
      // by a new HD-Player.exe).  Force-cancel it so the new "start" command
      // can succeed instead of returning "setup already running".
      if (s_running.load())
      {
          ULONGLONG age = GetTickCount64() - s_runStartTick.load();
          if (age > kMaxRunAge)
          {
              // Stale run — cancel silently and fall through to start a new one.
              Setup_Cancel();
          }
          else
          {
              // A genuinely active run is in progress — reject the duplicate.
              PipeWrite(hPipe, "err Setup is already running — wait for step 3 to complete");
              return;
          }
      }

      // Atomically claim s_running; bail if another thread beat us here.
      if (s_running.exchange(true)) return;

      s_cancelRequested.store(false);
      s_runStartTick.store(GetTickCount64());

      auto* ctx = new(std::nothrow) SetupCtx{ hPipe };
      if (!ctx) { s_running.store(false); return; }

      HANDLE h = CreateThread(nullptr, 0, SetupThread, ctx, 0, nullptr);
      if (!h)
      {
          delete ctx;
          s_running.store(false);
      }
      else
      {
          CloseHandle(h);
      }
  }

  bool Setup_IsRunning()
  {
      return s_running.load();
  }
  