#pragma once
#include <cstdint>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "vector.h"

// ============================================================
//  ESP logic — WorldToScreen + entity enumeration
//  Called from the memory-reading thread; results stored in a
//  double-buffered list that the overlay thread renders.
// ============================================================

// ---- Bone screen position ----------------------------------------
struct BonePos
{
    Vector2 screen;
    bool    valid = false;
};

// ---- Per-entity data passed to the renderer ---------------------
//  IMPROVEMENT: added health, isVisible, isKnocked fields
//               (reference EspGame.h — health bar + visibility check)
struct EspEntry
{
    Vector2 headScreen;
    Vector2 rootScreen;
    bool    visible    = false;    // Avatar_IsVisible byte = true
    bool    isKnocked  = false;    // XPose == 8 — player is knocked down
    bool    valid      = false;
    float   distanceM  = 0.f;
    float   health     = 0.f;     // HP value 0..200 (HP + EP)
    float   maxHealth  = 200.f;   // used for health bar normalisation

    // Guest virtual address of the entity — used by AimVisible to write
    // the head-collider handle into the entity's LegitCollider slot.
    uint32_t entityAddr = 0;

    // World-space bone positions — used by aimbot for rotation calculation.
    // Populated every frame regardless of skeleton-ESP toggle.
    Vector3 headWorld  {};
    Vector3 neckWorld  {};
    Vector3 hipWorld   {};

    // Player name (optional, Config::EspNameEnabled)
    char    name[64]   = {};   // 64 bytes — handles long Unicode names

    // Weapon display string (optional, Config::EspWeaponEnabled)
    // Format: "<ascii_icon> <weapon_name>" e.g. "--[=|=]--> AK47"
    // Built in esp.cpp from WeaponIndex::GetWeaponAscii + GetWeaponName.
    // Bot AK47 (id=0) is included; FIST (id=1) shown as "{-_-} FIST".
    char    weaponName[48] = {};

    // Skeleton bones (optional, Config::EspSkeletonEnabled)
    BonePos neck;
    BonePos spine;
    BonePos hip;
    BonePos lShoulder, rShoulder;
    BonePos lElbow,    rElbow;
    BonePos lWrist,    rWrist;
    BonePos lKnee,     rKnee;
    BonePos lFoot,     rFoot;
};

constexpr int MAX_ENTITIES = 100;

struct EspBuffer
{
    EspEntry entries[MAX_ENTITIES];
    int      count = 0;
};

void SwapEspBuffers();
EspBuffer GetFrontBuffer();

// Project a 3D world point onto 2D screen coords.
// Returns {-1,-1} if behind camera.
Vector2 WorldToScreen(const Matrix4x4& viewMatrix, const Vector3& worldPos,
                      int screenW, int screenH);

// Full entity-scan pass.  Fills back buffer, then swaps.
// Called from the memory thread (~30 Hz).
void UpdateEspData(int emulatorW, int emulatorH);

// Hard-reset all inter-frame tracking state (s_lastCurrentGame/Match, s_wasInMatch).
// Also flushes the GVA→GPA cache and publishes a blank buffer.
// Safe to call from the UI thread via Config::ClearCacheRequested.
void ResetEspState();
