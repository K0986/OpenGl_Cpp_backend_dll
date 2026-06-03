#pragma once
// gl_draw.h — All OpenGL 2D primitive helpers for the overlay.
// All functions must be called from the render thread (GL context owner).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <GL/gl.h>
#include <cstdint>
#include "esp.h"   // BonePos

// ── GL context setup ─────────────────────────────────────────────────────────
void GL_FillPFD(PIXELFORMATDESCRIPTOR& pfd, bool forOverlay);
void GL_Ortho2D(int w, int h);
GLuint GL_MakeFontLists(HDC dc, const char* fontName, int height,
                         int weight, int quality, int pitch, int* outH);

// ── Primitives ───────────────────────────────────────────────────────────────
// Each function is thin; use batch variants for performance-critical paths.
void GL_FilledRect(float x1, float y1, float x2, float y2,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void GL_Line(float x1, float y1, float x2, float y2,
             uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw = 1.5f);
void GL_CurvedLine(float x1, float y1, float x2, float y2,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw = 2.f);
void GL_Circle(float cx, float cy, float radius,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw = 1.f);
void GL_Box(float x1, float y1, float x2, float y2,
            uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw = 1.5f);
void GL_RoundedFilledRect(float x1, float y1, float x2, float y2,
                           float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void GL_RoundedStrokedRect(float x1, float y1, float x2, float y2,
                            float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                            float lw = 1.5f);
void GL_Text(GLuint fontBase, float x, float y, int fh,
             const char* text, float r, float g, float b, float a);

// ── Batched line renderer ─────────────────────────────────────────────────────
// Collects line segments and flushes them all in ONE glBegin(GL_LINES) call
// grouped by line width — dramatically reduces GL driver overhead with 60+ entities.
void GL_BatchReset();
void GL_BatchLine(float x1, float y1, float x2, float y2,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw);
void GL_BatchFlush();    // must be called once per frame after all GL_BatchLine calls

// ── Bone helper (calls GL_Line; safe to call with !a.valid or !b.valid) ──────
void GL_BoneLine(const BonePos& a, const BonePos& b, int ox, int oy,
                 uint8_t r, uint8_t g, uint8_t b_, uint8_t alpha, float lw);
