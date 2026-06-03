#pragma once
#include "vector.h"
#include <cstdint>

// Called from UpdateEspData() after SwapEspBuffers(), same as RunAimbot.
// When Config::AimVisibleEnabled is true, finds the closest on-screen enemy
// within the aimbot FOV/distance limits and writes its head-collider handle
// into its LegitCollider slot (entity + 0x54) — no keybind required.
// Reference: AimbotVisible.cs / AimAtTarget() from the C# reference code.
void RunAimVisible(const Matrix4x4& viewMatrix, const Vector3& camPos,
                   int screenW, int screenH);
