#pragma once

// Dedicated high-frequency SilentAim thread.
// Matches the diagram flow exactly:
//   while(running) → check enabled → check screen → scan entities →
//   find closest → check isFiring → read weapon data → write ray dir
//
// StartSilentAimThread() — call once from MainThread (dllmain.cpp)
// StopSilentAimThread()  — call on DLL detach / overlay exit
void StartSilentAimThread();
void StopSilentAimThread();
