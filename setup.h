#pragma once
  // setup.h — 3-step guided setup flow triggered by the "start" pipe command.
  // Only this module writes step-progress log messages to the pipe client.
  // Background threads (hooks, ADB) only update Config flags silently.
  #include <Windows.h>

  // Call when the "start" command arrives on the pipe.
  // hPipe: current named-pipe client handle.
  // Returns immediately; actual work happens on a background thread.
  // If a stale (timed-out) run is in progress it is silently cancelled first.
  void Setup_Start(HANDLE hPipe);

  // Returns true if a setup flow is currently running.
  bool Setup_IsRunning();

  // Cancel any in-progress setup run.  The running thread will exit its
  // WaitFor loops without sending an error message.  Blocks up to 2 s for
  // the thread to acknowledge.
  void Setup_Cancel();
  