#include "aimbot.h"
#include "config.h"
#include "esp.h"
#include "memory.h"
#include "offsets.h"
#include <cmath>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// ============================================================
//  ExtractCamPosFromViewMatrix
//
//  Camera world position P satisfies WorldToScreen(P) == screen
//  centre, so:
//    P.x*m00 + P.y*m10 + P.z*m20 + m30 = 0
//    P.x*m01 + P.y*m11 + P.z*m21 + m31 = 0
//    P.x*m02 + P.y*m12 + P.z*m22 + m32 = 0
//  Rearranged: A * P = b, solved with Cramer's rule.
// ============================================================
static Vector3 ExtractCamPosFromViewMatrix(const Matrix4x4& vm)
{
    float a00 = vm.m00, a01 = vm.m10, a02 = vm.m20, b0 = -vm.m30;
    float a10 = vm.m01, a11 = vm.m11, a12 = vm.m21, b1 = -vm.m31;
    float a20 = vm.m02, a21 = vm.m12, a22 = vm.m22, b2 = -vm.m32;

    float det = a00 * (a11 * a22 - a12 * a21)
              - a01 * (a10 * a22 - a12 * a20)
              + a02 * (a10 * a21 - a11 * a20);

    if (fabsf(det) < 1e-6f) return Vector3::Zero();
    float inv = 1.f / det;

    Vector3 P;
    P.x = inv * (  b0 * (a11 * a22 - a12 * a21)
                 - a01 * ( b1 * a22 - a12 *  b2)
                 + a02 * ( b1 * a21 - a11 *  b2));
    P.y = inv * (a00 * (  b1 * a22 - a12 *  b2)
                 -  b0 * (a10 * a22 - a12 * a20)
                 + a02 * (a10 *  b2 -  b1 * a20));
    P.z = inv * (a00 * (a11 *  b2 -  b1 * a21)
                 - a01 * (a10 *  b2 -  b1 * a20)
                 +  b0 * (a10 * a21 - a11 * a20));
    return P;
}

// ============================================================
//  MathUtils — exact port of MathUtils.cs (GetRotationToLocation /
//  LookRotation / FromToRotation) from the C# reference code.
//
//  Game engine writes a Unity Quaternion to AimRotation (0x404),
//  NOT a pitch/yaw Vector2.  Writing the wrong type was the
//  root cause of the aimbot being silently ignored by the game.
// ============================================================

constexpr float SMALL_FLOAT = 1e-10f;

static float QuatNorm(const Quaternion& q)
{
    return sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
}

static Quaternion QuatNormalized(const Quaternion& q)
{
    float n = QuatNorm(q);
    if (n < SMALL_FLOAT) return { 0.f, 0.f, 0.f, 1.f };
    float inv = 1.f / n;
    return { q.x * inv, q.y * inv, q.z * inv, q.w * inv };
}

// FromToRotation: minimal-arc rotation from 'from' to 'to'
static Quaternion FromToRotation(const Vector3& from, const Vector3& to)
{
    float dot = Vector3::Dot(from, to);
    float k   = sqrtf(from.LengthSq() * to.LengthSq());

    if (fabsf(dot / (k + SMALL_FLOAT) + 1.f) < 1e-5f) {
        // ~180 degrees — pick an orthogonal axis
        Vector3 orth = Vector3::Orthogonal(from).Normalized();
        return QuatNormalized({ orth.x, orth.y, orth.z, 0.f });
    }
    Vector3 cross = Vector3::Cross(from, to);
    return QuatNormalized({ cross.x, cross.y, cross.z, dot + k });
}

// LookRotation: exact port of MathUtils.LookRotation from MathUtils.cs
static Quaternion LookRotation(const Vector3& forwardsIn, const Vector3& upwardsIn)
{
    Vector3 forwards = forwardsIn.Normalized();
    Vector3 upwards  = upwardsIn.Normalized();

    if (forwards.LengthSq() < SMALL_FLOAT || upwards.LengthSq() < SMALL_FLOAT)
        return { 0.f, 0.f, 0.f, 1.f };   // identity

    // If nearly parallel to up, fall back to FromToRotation
    float absDot = fabsf(Vector3::Dot(forwards, upwards));
    if (1.f - absDot < SMALL_FLOAT)
        return FromToRotation(forwards, upwards);

    Vector3 right = Vector3::Normalized(Vector3::Cross(upwards, forwards));
    upwards = Vector3::Cross(forwards, right);

    // Build quaternion from orthonormal basis {right, up, forward}
    // Shepperd method — same branch logic as MathUtils.cs
    float radicand = right.x + upwards.y + forwards.z;
    Quaternion q;

    if (radicand > 0.f) {
        q.w = sqrtf(1.f + radicand) * 0.5f;
        float recip = 1.f / (4.f * q.w);
        q.x = (upwards.z - forwards.y) * recip;
        q.y = (forwards.x - right.z)   * recip;
        q.z = (right.y    - upwards.x) * recip;
    }
    else if (right.x >= upwards.y && right.x >= forwards.z) {
        q.x = sqrtf(1.f + right.x - upwards.y - forwards.z) * 0.5f;
        float recip = 1.f / (4.f * q.x);
        q.w = (upwards.z - forwards.y) * recip;
        q.z = (forwards.x + right.z)   * recip;
        q.y = (right.y    + upwards.x) * recip;
    }
    else if (upwards.y > forwards.z) {
        q.y = sqrtf(1.f - right.x + upwards.y - forwards.z) * 0.5f;
        float recip = 1.f / (4.f * q.y);
        q.z = (upwards.z + forwards.y) * recip;
        q.w = (forwards.x - right.z)   * recip;
        q.x = (right.y    + upwards.x) * recip;
    }
    else {
        q.z = sqrtf(1.f - right.x - upwards.y + forwards.z) * 0.5f;
        float recip = 1.f / (4.f * q.z);
        q.y = (upwards.z + forwards.y) * recip;
        q.x = (forwards.x + right.z)   * recip;
        q.w = (right.y    - upwards.x) * recip;
    }
    return q;
}

// GetRotationToLocation: exact port of MathUtils.GetRotationToLocation
// Reference: forwards = targetLocation + Vector3(0, y_bias, 0) - myLoc
//            return LookRotation(forwards, Vector3.Up)
static Quaternion GetRotationToLocation(const Vector3& targetLocation,
                                        float          y_bias,
                                        const Vector3& myLoc)
{
    Vector3 forwards = {
        targetLocation.x - myLoc.x,
        targetLocation.y + y_bias - myLoc.y,
        targetLocation.z - myLoc.z
    };
    Vector3 upwards = Vector3::Up();
    return LookRotation(forwards, upwards);
}

// ============================================================
//  RunAimbot
//  Called from the memory thread after SwapEspBuffers().
// ============================================================
void RunAimbot(uint32_t localPlayer, const Vector3& camPosHint,
               const Matrix4x4& viewMatrix, int screenW, int screenH)
{
    if (!Config::AimbotEnabled.load()) return;

    // Activation key: right mouse button (BlueStacks maps right-click to aim)
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0) return;

    if (localPlayer == 0) return;

    // Derive camera world position from view-projection matrix (mathematically
    // exact — no extra memory read required).  Falls back to the passed hint
    // (MainCameraTransform position) only if matrix extraction fails.
    Vector3 camPos = ExtractCamPosFromViewMatrix(viewMatrix);
    if (camPos.IsZero()) camPos = camPosHint;
    if (camPos.IsZero()) return;

    const float fov        = Config::AimFov.load();
    const float maxDistM   = Config::AimMaxDistance.load();
    const int   targetPart = Config::AimTargetPart.load();   // 0=Head 1=Neck 2=Hip
    const bool  ignKnocked = Config::AimIgnoreKnocked.load();

    // FOV limit: reference hard-codes 200 px; honour whichever is smaller
    const float fovcap = (fov > 0.f && fov < 200.f) ? fov : 200.f;

    float   bestCross = fovcap + 1.f;
    Vector3 bestWorld {};
    bool    found     = false;

    const float cx = screenW * 0.5f;
    const float cy = screenH * 0.5f;

    const EspBuffer buf = GetFrontBuffer();

    for (int i = 0; i < buf.count; ++i)
    {
        const EspEntry& e = buf.entries[i];
        if (!e.valid) continue;
        if (ignKnocked && e.isKnocked) continue;

        // Body-part selection — reference: Config.AimTargetPart switch
        // "HEAD" → entity.Head, "NECK" → entity.Neck, "HIP" → entity.Hip
        Vector3 tgt;
        switch (targetPart) {
        case 1:  tgt = e.neckWorld.IsZero() ? e.headWorld : e.neckWorld; break;
        case 2:  tgt = e.hipWorld.IsZero()  ? e.headWorld : e.hipWorld;  break;
        default: tgt = e.headWorld; break;
        }
        if (tgt.IsZero()) continue;

        // World-distance filter — reference: Config.AimBotMaxDistance
        if (Vector3::Distance(camPos, tgt) > maxDistM) continue;

        // Project to screen — reference: W2S.WorldToScreen
        Vector2 sc = WorldToScreen(viewMatrix, tgt, screenW, screenH);
        // W2S returns {-1,-1} for behind-camera; both < 1 → skip
        if (sc.x < 1.f || sc.y < 1.f) continue;

        // Crosshair distance in pixels — reference: crosshairDist
        float dx2  = sc.x - cx;
        float dy2  = sc.y - cy;
        float cross = sqrtf(dx2 * dx2 + dy2 * dy2);

        if (cross >= fovcap)    continue;   // outside FOV circle
        if (cross >= bestCross) continue;   // not closer than current best

        bestCross = cross;
        bestWorld = tgt;
        found     = true;
    }

    if (!found) return;

    // ── Write rotation to game memory ────────────────────────────────────────
    // Reference:
    //   var playerLook = MathUtils.GetRotationToLocation(target.Head, 0.1f,
    //                                                    Core.LocalMainCamera);
    //   InternalMemory.Write(Core.LocalPlayer + Offsets.AimRotation, playerLook);
    //
    // AimRotation expects a Unity Quaternion (x,y,z,w — 16 bytes).
    // Writing a Vector2 (8 bytes) was the root cause of the aimbot being
    // silently ignored by the engine.
    Quaternion rot = GetRotationToLocation(bestWorld, 0.1f, camPos);
    WriteZ<Quaternion>(localPlayer + (uint32_t)Offsets::AimRotation, rot);
}
