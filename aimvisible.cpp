#include "aimvisible.h"
#include "config.h"
#include "esp.h"
#include "memory.h"
#include "offsets.h"
#include <cmath>

// ============================================================
//  RunAimVisible
//
//  Exact port of AimbotVisible.cs from the C# reference:
//
//    FindBestTarget()
//      — iterates entities, picks the one whose head projects
//        closest to screen-centre within AimFov px AND within
//        AimMaxDistance world metres.  No key required.
//
//    AimAtTarget(entity)
//      1. Read  entity + 0x4A8 → m_HeadCollider  (RageCollider)
//      2. Write m_HeadCollider → entity + 0x54    (LegitCollider)
//         Repeated 11 times as in the reference (10 loop + 1 extra).
//
//  Unlike the rotation aimbot this never writes to localPlayer;
//  it writes directly to the *enemy* entity's collider slot so
//  the engine locks the bullet trace onto the head automatically.
// ============================================================
void RunAimVisible(const Matrix4x4& viewMatrix, const Vector3& camPos,
                   int screenW, int screenH)
{
    if (!Config::AimVisibleEnabled.load()) return;

    const float fov      = Config::AimFov.load();
    const float maxDistM = Config::AimMaxDistance.load();
    const bool ignKnocked = Config::AimIgnoreKnocked.load();

    // FOV cap matching aimbot (reference: crosshairDistance <= Config.Aimfov)
    const float fovcap = (fov > 0.f && fov < 200.f) ? fov : 200.f;

    const float cx = screenW * 0.5f;
    const float cy = screenH * 0.5f;

    float    bestCross  = fovcap + 1.f;
    uint32_t bestEntity = 0;

    const EspBuffer buf = GetFrontBuffer();

    // ── FindBestTarget ────────────────────────────────────────────────────────
    for (int i = 0; i < buf.count; ++i)
    {
        const EspEntry& e = buf.entries[i];
        if (!e.valid)       continue;
        if (e.entityAddr == 0) continue;
        if (ignKnocked && e.isKnocked) continue;

        // head2D — reference: W2S.WorldToScreen; skip if off-screen (< 1)
        Vector2 sc = WorldToScreen(viewMatrix, e.headWorld, screenW, screenH);
        if (sc.x < 1.f || sc.y < 1.f) continue;

        // World distance — reference: Vector3.Distance(Core.LocalMainCamera, entity.Head)
        float playerDist = Vector3::Distance(camPos, e.headWorld);
        if (playerDist > maxDistM) continue;

        // Crosshair distance in pixels
        float dx    = sc.x - cx;
        float dy    = sc.y - cy;
        float cross = sqrtf(dx * dx + dy * dy);

        if (cross > fovcap)     continue;   // outside FOV
        if (cross >= bestCross) continue;   // not closer

        bestCross  = cross;
        bestEntity = e.entityAddr;
    }

    if (bestEntity == 0) return;

    // ── AimAtTarget ───────────────────────────────────────────────────────────
    // Reference:
    //   var rHeadCollider = InternalMemory.Read<uint>(target.Address + 0x4A8, ...)
    //   for (int i = 0; i < 10; i++)
    //       InternalMemory.Write(target.Address + 0x54, m_HeadCollider);
    //   InternalMemory.Write(target.Address + 0x54, m_HeadCollider);  // +1 extra

    uint32_t headCollider = 0;
    if (!ReadZ(bestEntity + (uint32_t)Offsets::RageCollider, headCollider)
        || headCollider == 0)
        return;

    // 3 writes is enough to override any single engine tick that resets the slot.
    // (Reference used 11; reduced here for lower per-frame overhead.)
    for (int i = 0; i < 3; ++i)
        WriteZ<uint32_t>(bestEntity + (uint32_t)Offsets::LegitCollider, headCollider);
}
