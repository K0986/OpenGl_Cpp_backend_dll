#pragma once
#include "vector.h"
#include <cstdint>

// Called from UpdateEspData() immediately after SwapEspBuffers().
// Reads the front buffer, selects the closest visible enemy within the
// configured FOV radius, computes pitch/yaw rotation and writes it to
// localPlayer + Offsets::AimRotation (0x404) as a Vector2.
// Activation key: right mouse button (VK_RBUTTON).
// Camera world position is derived from the view-projection matrix directly,
// so it works even when the separate camera-transform memory read fails.
void RunAimbot(uint32_t localPlayer, const Vector3& camPosHint,
               const Matrix4x4& viewMatrix, int screenW, int screenH);
