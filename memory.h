#pragma once
#include <cstdint>
#include <string>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "vector.h"
#include "hooks.h"

// ---- Typed R/W helpers (reference ReadZ + ConvertZ) ----------------

template<typename T>
inline bool ReadZ(uintptr_t address, T& out)
{
    return GuestRead(address, &out, sizeof(T));
}

template<typename T>
inline bool WriteZ(uintptr_t address, const T& val)
{
    return GuestWrite(address, &val, sizeof(val));
}

// ---- World-position helpers ----------------------------------------

bool    GetNodePosition(uint32_t nodeTransform, Vector3& outPos);
Vector3 GetEntityRootPosition(uint32_t entity);

// ---- IL2CPP managed string -----------------------------------------

std::string ReadPlayerName(uint32_t namePtr);

// ---- IMPROVEMENT: health reading (reference EspGame.h HealdShieldEP) ----
// Reads the combined HP + shield + EP float from an entity and clamps
// it to [0, maxHP].  Returns 0.f if the read fails.
float ReadPlayerHealth(uint32_t entity, float maxHP = 200.f);
