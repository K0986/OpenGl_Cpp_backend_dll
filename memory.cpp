#include "memory.h"
#include "offsets.h"
#include <string>
#include <cstring>
#include <algorithm>

// ============================================================
//  Unity transform position (reference EspGame.h)
//  Full parent-chain traversal via TMatrix list + index array
// ============================================================
static bool GetPosition(uint32_t transform, Vector3& pos)
{
    pos = Vector3::Zero();

    uint32_t transformObjValue = 0;
    if (!ReadZ(transform + 0x8, transformObjValue) || transformObjValue == 0)
        return false;

    uint32_t indexValue = 0;
    if (!ReadZ(transformObjValue + 0x24, indexValue))
        return false;

    uint32_t matrixValue = 0;
    if (!ReadZ(transformObjValue + 0x20, matrixValue) || matrixValue == 0)
        return false;

    uint32_t matrixListValue = 0;
    if (!ReadZ(matrixValue + 0x18, matrixListValue) || matrixListValue == 0)
        return false;

    uint32_t matrixIndicesValue = 0;
    if (!ReadZ(matrixValue + 0x1C, matrixIndicesValue) || matrixIndicesValue == 0)
        return false;

    Vector3 resultValue{};
    if (!ReadZ(indexValue * 0x30 + matrixListValue, resultValue))
        return false;

    int transformIndexValue = 0;
    if (!ReadZ((uint32_t)(indexValue * 0x4 + matrixIndicesValue), transformIndexValue))
        return false;

    int tries = 0;
    while (transformIndexValue >= 0)
    {
        if (++tries >= 50) break;

        TMatrix tMat{};
        if (!ReadZ((uint32_t)(0x30 * transformIndexValue + matrixListValue), tMat))
            return false;

        const float rx = tMat.rotation.x, ry = tMat.rotation.y;
        const float rz = tMat.rotation.z, rw = tMat.rotation.w;

    // Quaternion rotation matrix application.
    // Bug #8 note: the double-negation pattern (e.g. -2.f subtracted)
    // matches the game engine's specific TRS memory layout and has been
    // verified against the C# reference code (EspGame.h).  Do NOT
    // "simplify" the signs without re-verifying bone positions in-game.
    const float sx = resultValue.x * tMat.scale.x;
        const float sy = resultValue.y * tMat.scale.y;
        const float sz = resultValue.z * tMat.scale.z;

        resultValue.x = tMat.position.x + sx
            + sx * ((ry * ry * -2.f) - (rz * rz * 2.f))
            + sy * ((rw * rz * -2.f) - (ry * rx * -2.f))
            + sz * ((rz * rx * 2.f)  - (rw * ry * -2.f));

        resultValue.y = tMat.position.y + sy
            + sx * ((rx * ry * 2.f)  - (rw * rz * -2.f))
            + sy * ((rz * rz * -2.f) - (rx * rx * 2.f))
            + sz * ((rw * rx * -2.f) - (rz * ry * -2.f));

        resultValue.z = tMat.position.z + sz
            + sx * ((rw * ry * -2.f) - (rx * rz * -2.f))
            + sy * ((ry * rz * 2.f)  - (rw * rx * -2.f))
            + sz * ((rx * rx * -2.f) - (ry * ry * 2.f));

        if (!ReadZ((uint32_t)(transformIndexValue * 0x4 + matrixIndicesValue),
                   transformIndexValue))
            return false;
    }

    pos = resultValue;
    return tries < 50;
}

bool GetNodePosition(uint32_t nodeTransform, Vector3& outPos)
{
    if (nodeTransform == 0) return false;

    uint32_t transformValue = 0;
    if (!ReadZ(nodeTransform + 0x8, transformValue) || transformValue == 0)
    {
        outPos = Vector3::Zero();
        return false;
    }
    return GetPosition(transformValue, outPos);
}

Vector3 GetEntityRootPosition(uint32_t entity)
{
    Vector3 pos = Vector3::Zero();
    uint32_t rootBone = 0;
    if (ReadZ(entity + (uint32_t)Offsets::Root, rootBone) && rootBone != 0)
        GetNodePosition(rootBone, pos);
    return pos;
}

// ============================================================
//  ReadPlayerName — IL2CPP managed string layout
// ============================================================
std::string ReadPlayerName(uint32_t namePtr)
{
    if (namePtr == 0) return "";

    int32_t length = 0;
    if (!ReadZ<int32_t>(namePtr + 0x08, length)) return "";
    if (length <= 0 || length > 64) return "";

    wchar_t buf[65] = {};
    for (int i = 0; i < length && i < 64; ++i)
    {
        uint16_t ch = 0;
        if (!ReadZ<uint16_t>(namePtr + 0x0C + i * 2, ch)) break;
        buf[i] = (wchar_t)ch;
    }
    buf[length] = L'\0';

    char narrow[128] = {};
    // Bug #7 fix: use CP_UTF8 so non-ASCII characters survive the roundtrip
    // to DrawNameLabel (which decodes as UTF-8).  CP_ACP silently dropped
    // CJK / Cyrillic / Arabic chars in previous builds.
    WideCharToMultiByte(CP_UTF8, 0, buf, length, narrow, 127, nullptr, nullptr);
    narrow[127] = '\0';
    return std::string(narrow);
}

// ============================================================
//  ReadPlayerHealth — IMPROVEMENT from EspGame.h reference
//
//  EspGame.h reads a float at entity + HealdShieldEP which stores
//  the player's HP combined with shield and EP.  We read the same
//  offset and clamp to [0, maxHP] before returning.
//
//  This powers the health bar drawn to the left of each ESP box
//  (controlled by Config::EspHealthEnabled).
// ============================================================
float ReadPlayerHealth(uint32_t entity, float maxHP)
{
    if (entity == 0) return 0.f;

    float hp = 0.f;
    if (!ReadZ(entity + (uint32_t)Offsets::HealdShieldEP, hp))
        return 0.f;

    // Clamp to sane range: game stores 0 → maxHP.
    // Negative means read garbage; values > 999 are impossible in FF.
    if (hp < 0.f)   hp = 0.f;
    if (hp > maxHP) hp = maxHP;
    return hp;
}
