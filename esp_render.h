#pragma once
// esp_render.h — Renders the ESP overlay for one frame.
// All functions run on the render thread.
#include <Windows.h>
#include <GL/gl.h>

// Called once after the GL context is current, before the render loop.
bool EspRender_Init(HDC overlayDC);
// Called once when the render loop exits (frees GL resources).
void EspRender_Shutdown();
// Draw all ESP elements for this frame into the current GL context.
// eLeft/eTop/eClientW/eClientH describe the emulator window position on screen.
void EspRender_Draw(int eLeft, int eTop, int eClientW, int eClientH);
