#pragma once
#include <cmath>

// ============================================================
//  Minimal math types used by ESP / WorldToScreen
//  BUG FIX (from EspGame.h analysis):
//   - Vector3::Normalized now guards against zero-magnitude
//     division.  EspGame.h's Normalized() called (v/Magnitude())
//     without checking Magnitude() > 0, producing NaN/Inf when
//     applied to the zero vector (e.g. a bone that hasn't moved).
//   - Magnitude() uses sqrtf() not sqrt() — float precision,
//     no implicit double promotion.
// ============================================================

// Forward declarations
struct Vector3;

struct Vector2
{
    float x = 0.f, y = 0.f;
    Vector2() = default;
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator-(const Vector2& o) const { return { x - o.x, y - o.y }; }
    Vector2 operator+(const Vector2& o) const { return { x + o.x, y + o.y }; }
    Vector2 operator*(float s)           const { return { x * s,   y * s   }; }

    bool IsValid() const { return x >= 0.f && y >= 0.f; }

    static float Distance(const Vector2& a, const Vector2& b)
    {
        float dx = a.x - b.x, dy = a.y - b.y;
        return sqrtf(dx * dx + dy * dy);
    }
};

struct Vector3
{
    float x = 0.f, y = 0.f, z = 0.f;
    Vector3() = default;
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator-(const Vector3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vector3 operator+(const Vector3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vector3 operator*(float s)           const { return { x * s,   y * s,   z * s   }; }

    // BUG FIX: use sqrtf (float), not sqrt (double), to avoid implicit promotion
    float Length()     const { return sqrtf(x * x + y * y + z * z); }
    float LengthSq()   const { return x * x + y * y + z * z; }
    bool  IsZero()     const { return x == 0.f && y == 0.f && z == 0.f; }

    // BUG FIX: guard zero-magnitude — EspGame.h divided without checking,
    //          producing NaN/Inf on zero vectors passed from uninit bone reads.
    Vector3 Normalized() const
    {
        float len = Length();
        if (len < 1e-7f) return *this;   // safe fallback: return unchanged
        float inv = 1.f / len;
        return { x * inv, y * inv, z * inv };
    }

    static float Distance(const Vector3& a, const Vector3& b) { return (a - b).Length(); }
    static float Dot(const Vector3& a, const Vector3& b)      { return a.x*b.x + a.y*b.y + a.z*b.z; }
    static Vector3 Cross(const Vector3& a, const Vector3& b)
    {
        return { a.y*b.z - a.z*b.y,
                 a.z*b.x - a.x*b.z,
                 a.x*b.y - a.y*b.x };
    }
    // BUG FIX: guards zero-mag (EspGame.h static version had the same issue)
    static Vector3 Normalized(const Vector3& v) { return v.Normalized(); }
    static float   SqrMagnitude(const Vector3& v) { return v.LengthSq(); }
    static Vector3 Zero()    { return {}; }
    static Vector3 Forward() { return {0.f, 0.f, 1.f}; }
    static Vector3 Up()      { return {0.f, 1.f, 0.f}; }
    static Vector3 Right()   { return {1.f, 0.f, 0.f}; }
    static Vector3 Orthogonal(const Vector3& v)
    {
        // Returns a vector orthogonal to v (used by Quaternion::FromToRotation)
        return fabsf(v.x) < 0.9f
            ? Cross(v, {1.f, 0.f, 0.f})
            : Cross(v, {0.f, 1.f, 0.f});
    }
};

// Row-major view-projection matrix (reference EspGame.h)
struct Matrix4x4
{
    float m00 = 0, m01 = 0, m02 = 0, m03 = 0;
    float m10 = 0, m11 = 0, m12 = 0, m13 = 0;
    float m20 = 0, m21 = 0, m22 = 0, m23 = 0;
    float m30 = 0, m31 = 0, m32 = 0, m33 = 0;
};

struct Vector4
{
    float x = 0, y = 0, z = 0, w = 0;
};

struct Quaternion
{
    float x = 0, y = 0, z = 0, w = 1;   // BUG FIX: w should default to 1 (identity)
};

// Unity TRS matrix node (memory.cpp transform traversal)
struct TMatrix
{
    Vector4    position;
    Quaternion rotation;
    Vector4    scale;
};
static_assert(sizeof(TMatrix) == 48, "TMatrix must be 48 bytes to match game memory layout (0x30 stride)");
static_assert(sizeof(Matrix4x4) == 64, "Matrix4x4 must be 64 bytes to match game view-matrix layout");
static_assert(sizeof(Vector3) == 12, "Vector3 must be 12 bytes");
static_assert(sizeof(Quaternion) == 16, "Quaternion must be 16 bytes");
