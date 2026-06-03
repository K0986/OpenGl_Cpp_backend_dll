// esp_render.cpp — ESP entity drawing and overlay HUD.
// Extracted from overlay.cpp for maintainability.
// Uses gl_draw.h batched primitives for minimal GL driver overhead.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <GL/gl.h>

#include "esp_render.h"
#include "esp.h"
#include "config.h"
#include "gl_draw.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifndef GL_BGRA_EXT
#  define GL_BGRA_EXT 0x80E1
#endif

// ── Overlay font (GL display lists) ──────────────────────────────────────────
static GLuint s_fontBase  = 0;
static int    s_fontH     = 12;
static bool   s_fontReady = false;

// ── Name-label GDI texture ────────────────────────────────────────────────────
static const int NAME_W = 256, NAME_H = 20;
static HDC     s_nameDC    = nullptr;
static HBITMAP s_nameDIB   = nullptr;
static BYTE*   s_namePx    = nullptr;
static GLuint  s_nameTex   = 0;
static HFONT   s_nameFont  = nullptr;

// ── ESP palette (matching original) ──────────────────────────────────────────
static const uint8_t kPrimR=0,   kPrimG=255, kPrimB=0;
static const uint8_t kBoxR =255, kBoxG =165, kBoxB =0;
static const uint8_t kSkelR=0,   kSkelG=220, kSkelB=255;
static const uint8_t kKnkR =255, kKnkG =255, kKnkB =0;
static const uint8_t kLavR =180, kLavG =150, kLavB =255;
static const uint8_t kFovR =0,   kFovG =255, kFovB =255;

// ── Name label helpers ────────────────────────────────────────────────────────
static bool InitNameTex()
{
    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = NAME_W;
    bi.bmiHeader.biHeight      = -NAME_H;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    s_nameDIB = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!s_nameDIB) return false;
    s_namePx = static_cast<BYTE*>(pBits);

    s_nameDC = CreateCompatibleDC(nullptr);
    SelectObject(s_nameDC, s_nameDIB);

    s_nameFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Segoe UI");
    if (!s_nameFont)
        s_nameFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH|FF_DONTCARE, "Tahoma");

    glGenTextures(1, &s_nameTex);
    glBindTexture(GL_TEXTURE_2D, s_nameTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, NAME_W, NAME_H, 0,
                 GL_BGRA_EXT, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

static void CleanupNameTex()
{
    if (s_nameTex)  { glDeleteTextures(1, &s_nameTex); s_nameTex = 0; }
    if (s_nameFont) { DeleteObject(s_nameFont); s_nameFont = nullptr; }
    if (s_nameDC)   { DeleteDC(s_nameDC);       s_nameDC   = nullptr; }
    if (s_nameDIB)  { DeleteObject(s_nameDIB);  s_nameDIB  = nullptr; }
    s_namePx = nullptr;
}

static void DrawNameLabel(float x, float y, const char* utf8)
{
    if (!s_nameTex || !s_nameDC || !utf8 || !utf8[0]) return;

    wchar_t wbuf[128] = {};
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, wbuf, 128);
    if (wlen <= 0) {
        int wi = 0;
        for (int i = 0; utf8[i] && wi < 127; ++i)
            wbuf[wi++] = ((unsigned char)utf8[i] < 128) ? (wchar_t)utf8[i] : L'?';
        wbuf[wi] = L'\0';
    }
    if (!wbuf[0]) return;

    HFONT old = (HFONT)SelectObject(s_nameDC, s_nameFont);
    SIZE  ts  = {};
    int   sl  = (int)wcslen(wbuf);
    GetTextExtentPoint32W(s_nameDC, wbuf, sl, &ts);
    SelectObject(s_nameDC, old);

    const int hPad = 6, vPad = 2;
    int labelW = std::min((int)ts.cx + hPad*2, NAME_W);
    int labelH = std::min((int)ts.cy + vPad*2, NAME_H);
    if (labelW < 8) labelW = 8;
    if (labelH < 8) labelH = 8;

    memset(s_namePx, 0, (size_t)NAME_W * NAME_H * 4);
    old = (HFONT)SelectObject(s_nameDC, s_nameFont);
    SetBkMode(s_nameDC, TRANSPARENT);
    RECT rc = {hPad, 0, labelW-hPad, labelH};
    SetTextColor(s_nameDC, RGB(255,255,255));
    DrawTextW(s_nameDC, wbuf, -1, &rc, DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
    SelectObject(s_nameDC, old);
    GdiFlush();

    struct BGRA32 { BYTE b, g, r, a; };
    BGRA32* px = reinterpret_cast<BGRA32*>(s_namePx);
    const int npx = NAME_W * NAME_H;
    for (int i = 0; i < npx; ++i)
        px[i].a = (px[i].r | px[i].g | px[i].b) ? 255 : 0;

    glBindTexture(GL_TEXTURE_2D, s_nameTex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, NAME_W, NAME_H,
                    GL_BGRA_EXT, GL_UNSIGNED_BYTE, s_namePx);

    const float u  = (float)labelW / (float)NAME_W;
    const float v  = (float)labelH / (float)NAME_H;
    const float lw = (float)labelW, lh = (float)labelH;
    const float cx = x + (float)NAME_W*0.5f - lw*0.5f;

    glDisable(GL_TEXTURE_2D);
    glColor4ub(1, 1, 1, 255);
    glBegin(GL_QUADS);
      glVertex2f(cx,    y); glVertex2f(cx+lw, y);
      glVertex2f(cx+lw, y+lh); glVertex2f(cx, y+lh);
    glEnd();

    glEnable(GL_TEXTURE_2D);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glBegin(GL_QUADS);
      glTexCoord2f(0.f, 0.f); glVertex2f(cx,    y);
      glTexCoord2f(u,   0.f); glVertex2f(cx+lw, y);
      glTexCoord2f(u,   v);   glVertex2f(cx+lw, y+lh);
      glTexCoord2f(0.f, v);   glVertex2f(cx,    y+lh);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ── Public API ────────────────────────────────────────────────────────────────

bool EspRender_Init(HDC overlayDC)
{
    s_fontBase  = GL_MakeFontLists(overlayDC, "Consolas", -9, FW_NORMAL,
                                    CLEARTYPE_QUALITY, FIXED_PITCH, &s_fontH);
    s_fontReady = (s_fontBase != 0);
    return InitNameTex();
}

void EspRender_Shutdown()
{
    CleanupNameTex();
    if (s_fontBase) { glDeleteLists(s_fontBase, 256); s_fontBase = 0; }
    s_fontReady = false;
}

void EspRender_Draw(int eLeft, int eTop, int eClientW, int eClientH)
{
    const bool doLines   = Config::EspLinesEnabled.load();
    const bool doBox     = Config::EspBoxEnabled.load();
    const bool doSkel    = Config::EspSkeletonEnabled.load();
    const bool doNames   = Config::EspNameEnabled.load();
    const bool doHealth  = Config::EspHealthEnabled.load();
    const bool doKnocked = Config::ShowKnockedTint.load();
    const bool doWeapon  = Config::EspWeaponEnabled.load();
    const float skelTk   = Config::SkeletonThickness.load();
    const bool  doFOV    = Config::AimFovEnabled.load();
    const float fovR     = Config::AimFov.load();

    GL_BatchReset();

    if (doLines || doBox || doSkel || doNames || doWeapon || doHealth)
    {
        const EspBuffer buf = GetFrontBuffer();

        for (int i = 0; i < buf.count && i < MAX_ENTITIES; ++i)
        {
            const EspEntry& e = buf.entries[i];
            if (!e.valid) continue;

            const float hx = e.headScreen.x + eLeft;
            const float hy = e.headScreen.y + eTop;
            const float rx = e.rootScreen.x + eLeft;
            const float ry = e.rootScreen.y + eTop;

            const float playerH  = std::max(20.f, fabsf(ry - hy));
            const float boxHalf  = std::max(8.f, playerH * Config::BoxWidthRatio);
            const float minX     = hx - boxHalf;
            const float maxX     = hx + boxHalf;
            const float minY     = hy - playerH * 0.08f;
            const float maxY     = ry + playerH * 0.06f;

            uint8_t eR=kPrimR, eG=kPrimG, eB=kPrimB;
            uint8_t bR=kBoxR,  bG=kBoxG,  bB=kBoxB;
            uint8_t sR=kSkelR, sG=kSkelG, sB=kSkelB;
            if (e.isKnocked && doKnocked) {
                eR=kKnkR; eG=kKnkG; eB=kKnkB;
                bR=kKnkR; bG=kKnkG; bB=kKnkB;
                sR=kKnkR; sG=kKnkG; sB=kKnkB;
            }
            const uint8_t ALPHA = 220;

            // Corner-bracket box — all 16 line segments go into the batch
            if (doBox) {
                const float cLen = std::min(14.f, (maxY-minY)*0.22f);
                const float ct   = 2.8f;
                // Shadow brackets
                GL_BatchLine(minX-1,minY-1, minX+cLen,minY-1,   1,1,1,90,3.5f);
                GL_BatchLine(minX-1,minY-1, minX-1,minY+cLen,   1,1,1,90,3.5f);
                GL_BatchLine(maxX+1,minY-1, maxX-cLen,minY-1,   1,1,1,90,3.5f);
                GL_BatchLine(maxX+1,minY-1, maxX+1,minY+cLen,   1,1,1,90,3.5f);
                GL_BatchLine(minX-1,maxY+1, minX+cLen,maxY+1,   1,1,1,90,3.5f);
                GL_BatchLine(minX-1,maxY+1, minX-1,maxY-cLen,   1,1,1,90,3.5f);
                GL_BatchLine(maxX+1,maxY+1, maxX-cLen,maxY+1,   1,1,1,90,3.5f);
                GL_BatchLine(maxX+1,maxY+1, maxX+1,maxY-cLen,   1,1,1,90,3.5f);
                // Colour brackets
                GL_BatchLine(minX,minY, minX+cLen,minY, bR,bG,bB,255,ct);
                GL_BatchLine(minX,minY, minX,minY+cLen, bR,bG,bB,255,ct);
                GL_BatchLine(maxX,minY, maxX-cLen,minY, bR,bG,bB,255,ct);
                GL_BatchLine(maxX,minY, maxX,minY+cLen, bR,bG,bB,255,ct);
                GL_BatchLine(minX,maxY, minX+cLen,maxY, bR,bG,bB,255,ct);
                GL_BatchLine(minX,maxY, minX,maxY-cLen, bR,bG,bB,255,ct);
                GL_BatchLine(maxX,maxY, maxX-cLen,maxY, bR,bG,bB,255,ct);
                GL_BatchLine(maxX,maxY, maxX,maxY-cLen, bR,bG,bB,255,ct);
            }

            // Curved snap line — not batchable (GL_LINE_STRIP), call directly
            if (doLines) {
                float lx = eLeft + (float)eClientW * 0.5f;
                float ly = (float)eTop;
                GL_CurvedLine(lx, ly, hx, hy, eR, eG, eB, 200, 2.f);
            }

            // Skeleton — all bone segments go into the batch
            if (doSkel) {
                GL_BoneLine(e.neck,      e.spine,     eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.spine,     e.hip,       eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.neck,      e.lShoulder, eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.lShoulder, e.lElbow,    eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.lElbow,    e.lWrist,    eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.neck,      e.rShoulder, eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.rShoulder, e.rElbow,    eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.rElbow,    e.rWrist,    eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.hip,       e.lKnee,     eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.lKnee,     e.lFoot,     eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.hip,       e.rKnee,     eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                GL_BoneLine(e.rKnee,     e.rFoot,     eLeft,eTop,sR,sG,sB,ALPHA,skelTk);
                float headR = std::max(4.f, boxHalf*0.4f);
                GL_Circle(hx, hy, headR, sR, sG, sB, 160, skelTk);
            }

            // Health bar (immediate — uses GL_QUADS, not affected by line batch)
            if (doBox && e.maxHealth > 0.f) {
                float ratio   = std::max(0.f, std::min(1.f, e.health / e.maxHealth));
                float barX2   = minX - Config::HealthBarGap;
                float barX1   = barX2 - Config::HealthBarW;
                float barH    = maxY - minY;
                float fillH   = barH * ratio;
                float fillTop = minY + (barH - fillH);

                GL_FilledRect(barX1, minY, barX2, maxY, 10, 10, 10, 220);
                if (ratio > 0.f) {
                    uint8_t hr = 255;
                    uint8_t hg = (uint8_t)(255.f * ratio);
                    GL_FilledRect(barX1, fillTop, barX2, maxY, hr, hg, 0, 245);
                }
                GL_BatchLine(barX1,minY, barX2,minY, 80,80,80,200,1.f);
                GL_BatchLine(barX2,minY, barX2,maxY, 80,80,80,200,1.f);
                GL_BatchLine(barX2,maxY, barX1,maxY, 80,80,80,200,1.f);
                GL_BatchLine(barX1,maxY, barX1,minY, 80,80,80,200,1.f);
            }

            // Name label
            if (doNames && e.name[0] != '\0') {
                float nx = hx - NAME_W * 0.5f;
                float ny = minY - (float)NAME_H - 3.f;
                DrawNameLabel(nx, ny, e.name);
            }

            // Distance label
            if (s_fontReady && e.distanceM > 1.f) {
                char db[16];
                snprintf(db, sizeof(db), "%.0fm", e.distanceM);
                float tw = (float)strlen(db) * 6.f;
                float dx = hx - tw*0.5f, dy = maxY + 3.f;
                GL_Text(s_fontBase, dx+1,dy+1, s_fontH, db, 0.15f,0.10f,0.28f,0.55f);
                GL_Text(s_fontBase, dx,  dy,   s_fontH, db,
                        kLavR/255.f, kLavG/255.f, kLavB/255.f, 0.90f);
            }

            // Weapon name
            if (doWeapon && s_fontReady && e.weaponName[0] != '\0') {
                COLORREF wc = Config::WeaponNameColor.load();
                float wR = GetRValue(wc)/255.f, wG = GetGValue(wc)/255.f, wB2 = GetBValue(wc)/255.f;
                float tw = (float)strlen(e.weaponName) * 6.f;
                float wx = hx - tw*0.5f, wy = maxY + 16.f;
                GL_Text(s_fontBase, wx+1,wy+1, s_fontH, e.weaponName, 0,0,0,0.70f);
                GL_Text(s_fontBase, wx,  wy,   s_fontH, e.weaponName, wR,wG,wB2,0.95f);
            }
        }
    }

    // Flush all batched lines (all entities, sorted by lw — single GL call per width group)
    GL_BatchFlush();

    // FOV circle + crosshair (immediate — GL_LINE_LOOP)
    if (doFOV && fovR > 1.f) {
        float cx = eLeft + (float)eClientW * 0.5f;
        float cy = eTop  + (float)eClientH * 0.5f;
        GL_Circle(cx, cy, fovR, kFovR, kFovG, kFovB, 220, 1.5f);
        const float chLen = 10.f;
        GL_Line(cx-chLen, cy, cx+chLen, cy, kFovR, kFovG, kFovB, 255, 2.f);
        GL_Line(cx, cy-chLen, cx, cy+chLen, kFovR, kFovG, kFovB, 255, 2.f);
    }


}
