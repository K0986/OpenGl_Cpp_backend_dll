#pragma once
#include <cstdint>

// ============================================================
//  FreeFire Normal (com.dts.freefireth) offsets
//  Verified against C# reference (Bones.cs / Offsets.cs)
// ============================================================

namespace Offsets
{
    // ------------------------------------------------------------------
    //  Static-class / module layout
    // ------------------------------------------------------------------
    constexpr uintptr_t InitBase           = 0x9EC1C48;
    constexpr uintptr_t StaticClass        = 0x5C;
    constexpr uintptr_t DictionaryEntities = 0x68;

    // ------------------------------------------------------------------
    //  Match
    // ------------------------------------------------------------------
    constexpr uintptr_t CurrentMatch  = 0x50;
    constexpr uintptr_t MatchStatus   = 0x8C;

    // ------------------------------------------------------------------
    //  Players / Entities
    // ------------------------------------------------------------------
    constexpr uintptr_t LocalPlayer       = 0x94;
    constexpr uintptr_t Player_IsDead     = 0x50;
    constexpr uintptr_t Player_Name       = 0x2E4;
    constexpr uintptr_t Player_Data       = 0x48;
    constexpr uintptr_t Player_ShadowBase = 0x16BC;
    constexpr uintptr_t XPose             = 0x78;

    // ------------------------------------------------------------------
    //  Avatar / Team
    // ------------------------------------------------------------------
    constexpr uintptr_t AvatarManager      = 0x4C4;
    constexpr uintptr_t Avatar             = 0xA0;
    constexpr uintptr_t Avatar_IsVisible   = 0x95;   // bool — enemy is not occluded
    constexpr uintptr_t Avatar_Data        = 0x14;
    constexpr uintptr_t Avatar_Data_IsTeam = 0x59;
    constexpr uintptr_t HealdShieldEP      = 0x10;   // float — HP + shield + EP packed

    // ------------------------------------------------------------------
    //  Camera
    // ------------------------------------------------------------------
    constexpr uintptr_t FollowCamera         = 0x454;
    constexpr uintptr_t Camera               = 0x18;
    constexpr uintptr_t AimRotation          = 0x404;
    constexpr uintptr_t MainCameraTransform  = 0x254;
    constexpr uintptr_t ViewMatrix           = 0xE8;

    // ------------------------------------------------------------------
    //  Weapons
    // ------------------------------------------------------------------
    constexpr uintptr_t Weapon       = 0x3F8;
    constexpr uintptr_t WeaponData   = 0x58;
    constexpr uintptr_t WeaponRecoil = 0x0C;

    // ------------------------------------------------------------------
    //  Weapon hacks
    // ------------------------------------------------------------------
    constexpr uintptr_t NoReloadOffset  = 0x89;   // byte patch: disable reload
    constexpr uintptr_t isFiringOffset  = 0x544;  // bool — currently firing

    // ------------------------------------------------------------------
    //  Misc
    // ------------------------------------------------------------------
    constexpr uintptr_t LocalPlayerAttributes = 0x4C0;
    constexpr uintptr_t WukongOrion           = 0xB50;
    constexpr uintptr_t RageCollider          = 0x4A8;
    constexpr uintptr_t LegitCollider         = 0x54;
    constexpr uintptr_t BaseProfileInfo       = 0x15F4;

    // ------------------------------------------------------------------
    //  Silent-aim
    //  Source: KCBRUTALSILENT.cs — sAim1/2/3/4
    // ------------------------------------------------------------------
    constexpr uintptr_t AimSilent1 = 0x544;  // Player::IsFiring        (bool)   — WAS 0x8F0 (wrong)
    constexpr uintptr_t AimSilent2 = 0x948;  // Player::AimInfo         (uint32) — pointer to AimInfo struct
    constexpr uintptr_t AimSilent3 = 0x38;   // AimInfo::StartPos       (Vector3)
    constexpr uintptr_t AimSilent4 = 0x2C;   // AimInfo::RayDir         (Vector3) — bullet ray direction

    // ------------------------------------------------------------------
    //  Bones (FF Normal)
    //  BUG FIX: was LeftSholder (missing 'u') — now LeftShoulder
    // ------------------------------------------------------------------
    constexpr uintptr_t Head  = 0x45C;
    constexpr uintptr_t Neck  = 0x464;
    constexpr uintptr_t Spine = 0x600;
    constexpr uintptr_t Root  = 0x470;
    constexpr uintptr_t Hip   = 0x460;

    constexpr uintptr_t LeftShoulder  = 0x490;   // WAS: LeftSholder (typo — missing 'u')
    constexpr uintptr_t LeftElbow     = 0x4A4;
    constexpr uintptr_t LeftWrist     = 0x49C;
    constexpr uintptr_t LeftHand      = 0x488;

    constexpr uintptr_t RightShoulder = 0x494;   // WAS: RightSholder (typo — missing 'u')
    constexpr uintptr_t RightElbow    = 0x4A0;
    constexpr uintptr_t RightWrist    = 0x498;
    constexpr uintptr_t RightHand     = 0x458;

    constexpr uintptr_t LeftCalf   = 0x478;
    constexpr uintptr_t LeftFoot   = 0x480;
    constexpr uintptr_t RightCalf  = 0x47C;
    constexpr uintptr_t RightFoot  = 0x484;

    // ------------------------------------------------------------------
    //  Entity list / Dictionary layout constants
    //  C# Dictionary<int,Entity> internal layout:
    //    +0x08  _buckets (int[])
    //    +0x0C  _entries (Entry[])  — pointer to the il2cpp array object
    //    +0x10  _count              — allocated capacity (NOT live count)
    //  Each Entry is 16 bytes (stride 0x10):
    //    +0x00  hashCode (int32)   — negative means freed / tombstone
    //    +0x04  next     (int32)   — free-list index, -1 = end
    //    +0x08  key      (int32)   — dictionary key (ignored)
    //    +0x0C  value    (uint32)  — entity POINTER — what we use
    // ------------------------------------------------------------------
    constexpr uintptr_t EntityStride  = 0x10;
    constexpr uintptr_t EntityPtrOff  = 0x0C;
}
