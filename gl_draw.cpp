// gl_draw.cpp — OpenGL 2D drawing primitives + batched line renderer.
// All functions must be called from the render thread.

#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "gdi32.lib")

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <GL/gl.h>
#include "gl_draw.h"

#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>

// ─── GL context helpers ───────────────────────────────────────────────────────

void GL_FillPFD(PIXELFORMATDESCRIPTOR& pfd, bool forOverlay)
{
    ZeroMemory(&pfd, sizeof(pfd));
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    if (forOverlay) pfd.dwFlags |= PFD_SUPPORT_COMPOSITION;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cAlphaBits = forOverlay ? 8 : 0;
    pfd.cDepthBits = 16;
}

void GL_Ortho2D(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

GLuint GL_MakeFontLists(HDC dc, const char* fontName, int height,
                          int weight, int quality, int pitch, int* outH)
{
    HFONT hf = CreateFontA(height, 0, 0, 0, weight, 0, 0, 0,
                            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            quality, pitch, fontName);
    HFONT old = (HFONT)SelectObject(dc, hf);
    TEXTMETRICA tm{};
    GetTextMetricsA(dc, &tm);
    if (outH) *outH = tm.tmHeight;
    GLuint base = glGenLists(256);
    wglUseFontBitmapsA(dc, 0, 256, base);
    SelectObject(dc, old);
    DeleteObject(hf);
    return base;
}

// ─── Primitives ───────────────────────────────────────────────────────────────

void GL_FilledRect(float x1, float y1, float x2, float y2,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    glColor4ub(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x1, y1); glVertex2f(x2, y1);
    glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
}

void GL_Line(float x1, float y1, float x2, float y2,
             uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw)
{
    glLineWidth(lw);
    glColor4ub(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x1, y1); glVertex2f(x2, y2);
    glEnd();
}

void GL_CurvedLine(float x1, float y1, float x2, float y2,
                   uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw)
{
    const int SEGS = 16;
    float cpx = (x1 + x2) * 0.5f;
    float cpy = (y1 + y2) * 0.5f + 40.f;

    // Shadow pass
    glLineWidth(lw + 2.f);
    glColor4ub(0, 0, 0, 70);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= SEGS; ++i) {
        float t = i / (float)SEGS, mt = 1.f - t;
        glVertex2f(mt*mt*x1 + 2.f*mt*t*cpx + t*t*x2,
                   mt*mt*y1 + 2.f*mt*t*cpy + t*t*y2);
    }
    glEnd();

    // Colour pass
    glLineWidth(lw);
    glColor4ub(r, g, b, a);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= SEGS; ++i) {
        float t = i / (float)SEGS, mt = 1.f - t;
        glVertex2f(mt*mt*x1 + 2.f*mt*t*cpx + t*t*x2,
                   mt*mt*y1 + 2.f*mt*t*cpy + t*t*y2);
    }
    glEnd();
}

void GL_Circle(float cx, float cy, float radius,
               uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw)
{
    const int SEGS = 64;
    glLineWidth(lw);
    glColor4ub(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < SEGS; ++i) {
        float angle = i * 6.28318530f / SEGS;
        glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
    }
    glEnd();
}

void GL_Box(float x1, float y1, float x2, float y2,
            uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw)
{
    glLineWidth(lw);
    glColor4ub(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    glVertex2f(x1, y1); glVertex2f(x2, y1);
    glVertex2f(x2, y2); glVertex2f(x1, y2);
    glEnd();
}

void GL_RoundedFilledRect(float x1, float y1, float x2, float y2,
                           float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const int   N       = 10;
    const float HALF_PI = 1.5707963268f;
    float maxR = std::min((x2-x1)*0.5f, (y2-y1)*0.5f);
    if (radius > maxR) radius = maxR;
    if (radius < 0.f)  radius = 0.f;

    glColor4ub(r, g, b, a);
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f((x1+x2)*0.5f, (y1+y2)*0.5f);
    struct C { float cx, cy, a0; };
    C corners[4] = {
        {x2-radius, y1+radius, -HALF_PI      },
        {x2-radius, y2-radius,  0.f          },
        {x1+radius, y2-radius,  HALF_PI      },
        {x1+radius, y1+radius,  2.f*HALF_PI  },
    };
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i <= N; ++i) {
            float ang = corners[c].a0 + HALF_PI*(float)i/(float)N;
            glVertex2f(corners[c].cx + cosf(ang)*radius,
                       corners[c].cy + sinf(ang)*radius);
        }
    glVertex2f(corners[0].cx + cosf(corners[0].a0)*radius,
               corners[0].cy + sinf(corners[0].a0)*radius);
    glEnd();
}

void GL_RoundedStrokedRect(float x1, float y1, float x2, float y2,
                            float radius, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                            float lw)
{
    const int   N       = 10;
    const float HALF_PI = 1.5707963268f;
    float maxR = std::min((x2-x1)*0.5f, (y2-y1)*0.5f);
    if (radius > maxR) radius = maxR;
    if (radius < 0.f)  radius = 0.f;

    glLineWidth(lw);
    glColor4ub(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    struct C { float cx, cy, a0; };
    C corners[4] = {
        {x2-radius, y1+radius, -HALF_PI      },
        {x2-radius, y2-radius,  0.f          },
        {x1+radius, y2-radius,  HALF_PI      },
        {x1+radius, y1+radius,  2.f*HALF_PI  },
    };
    for (int c = 0; c < 4; ++c)
        for (int i = 0; i <= N; ++i) {
            float ang = corners[c].a0 + HALF_PI*(float)i/(float)N;
            glVertex2f(corners[c].cx + cosf(ang)*radius,
                       corners[c].cy + sinf(ang)*radius);
        }
    glEnd();
}

void GL_Text(GLuint fontBase, float x, float y, int fh,
             const char* text, float r, float g, float b, float a)
{
    if (!text || !*text || fontBase == 0) return;
    glColor4f(r, g, b, a);
    glRasterPos2f(x, y + fh - 2.f);
    glListBase(fontBase);
    glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, text);
}

// ─── Batched line renderer ─────────────────────────────────────────────────────
// Groups segments by line-width bucket and emits each group in one GL_LINES call.
// Typical game with 60 entities reduces from ~500+ glBegin/glEnd pairs to ~3.

struct BatchSeg {
    float  x1, y1, x2, y2;
    uint8_t r, g, b, a;
    float  lw;
};

static std::vector<BatchSeg> s_lineBatch;

void GL_BatchReset()
{
    s_lineBatch.clear();
}

void GL_BatchLine(float x1, float y1, float x2, float y2,
                  uint8_t r, uint8_t g, uint8_t b, uint8_t a, float lw)
{
    s_lineBatch.push_back({x1, y1, x2, y2, r, g, b, a, lw});
}

void GL_BatchFlush()
{
    if (s_lineBatch.empty()) return;

    // Sort by line width to minimise glLineWidth state changes
    std::sort(s_lineBatch.begin(), s_lineBatch.end(),
              [](const BatchSeg& a, const BatchSeg& b){ return a.lw < b.lw; });

    float curLw = -1.f;
    bool  inBegin = false;

    for (const auto& s : s_lineBatch)
    {
        if (s.lw != curLw)
        {
            if (inBegin) { glEnd(); inBegin = false; }
            glLineWidth(s.lw);
            curLw = s.lw;
            glBegin(GL_LINES);
            inBegin = true;
        }
        glColor4ub(s.r, s.g, s.b, s.a);
        glVertex2f(s.x1, s.y1);
        glVertex2f(s.x2, s.y2);
    }
    if (inBegin) glEnd();

    s_lineBatch.clear();
}

// ─── Bone helper ─────────────────────────────────────────────────────────────

void GL_BoneLine(const BonePos& a, const BonePos& b, int ox, int oy,
                 uint8_t r, uint8_t g, uint8_t b_, uint8_t alpha, float lw)
{
    if (!a.valid || !b.valid) return;
    GL_BatchLine(a.screen.x + ox, a.screen.y + oy,
                 b.screen.x + ox, b.screen.y + oy,
                 r, g, b_, alpha, lw);
}
