#pragma once
#include <Windows.h>

// Run the full-screen transparent ESP overlay.
// Blocks on the Windows message loop until the DLL unloads.
int  RunOverlay(HINSTANCE hInst);

// Wake up the overlay render thread (thread-safe).
void PostRedraw();

// Forward a status update to the pipe client and update the on-screen HUD.
// mainLine : short category label  (e.g. "ADB", "Connected", "FAILED")
// stepLine : longer detail string  (first line shown; \n breaks are stripped)
void UiSetStatus(const char* mainLine, const char* stepLine);
