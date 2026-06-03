#include <cmath>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <cstdio>
#include "config.h"
#include "thread_pool.h"
extern ThreadPool* g_threadPool; // defined in dllmain.cpp
#include "esp.h"
#include "memory.h"
#include "hooks.h"
#include "offsets.h"
#include "aimbot.h"
#include "aimvisible.h"
#include "weaponindex.h"
// silentaim.h removed — SilentAim now runs on its own dedicated thread (dllmain.cpp)

// ── Double buffer ─────────────────────────────────────────────────────────────
static EspBuffer  s_buf[2];
static int        s_writeIdx = 0;
static std::mutex s_swapMtx;

// ── Bool3: mirrors the C# reference code's entity team-state machine ──────────
//   Unknown → first time we've seen this entity address (check visibility next frame)
//   True    → confirmed teammate (skip forever)
//   False   → confirmed enemy (draw when alive)
enum class Bool3 { Unknown, True, False };

struct CachedEntity {
    Bool3 isTeam  = Bool3::Unknown;
    bool  isKnown = false;   // set only when isTeam confirmed False
};

// Keyed by entity GVA.  Cleared whenever the game mode / match / dict changes.
static std::unordered_map<uint32_t, CachedEntity> s_entityCache;

// ── Inter-frame tracking — all track GVA VALUES, not addresses ────────────────
static uint32_t s_lastCurrentGame  = 0;
static uint32_t s_lastCurrentMatch = 0;
static uint32_t s_lastEntityDict   = 0;
static uint32_t s_lastLocalPlayer  = 0;
static bool     s_wasInMatch       = false;

// savedLocalPlayer: cache last valid pointer so a momentary null during
// respawn / Training animation doesn't blank the ESP for one frame.
static uint32_t s_savedLocalPlayer = 0;

// savedViewMatrix: reuse last good matrix if the camera chain misses one frame.
static Matrix4x4 s_savedViewMatrix{};
static bool      s_hasViewMatrix = false;

static int s_matchStableFrames  = 0;
static constexpr int SETTLE_FRAMES = 1;

// ── Full state reset (ClearCache button or mode transition) ───────────────────
void ResetEspState()
{
    s_lastCurrentGame  = 0;
    s_lastCurrentMatch = 0;
    s_lastEntityDict   = 0;
    s_lastLocalPlayer  = 0;
    s_wasInMatch       = false;
    s_matchStableFrames = 0;
    s_savedLocalPlayer = 0;
    s_savedViewMatrix  = Matrix4x4{};
    s_hasViewMatrix    = false;
    s_entityCache.clear();
    InvalidatePhysCache();

    std::lock_guard<std::mutex> lk(s_swapMtx);
    s_buf[0].count = 0;  memset(s_buf[0].entries, 0, sizeof(s_buf[0].entries));
    s_buf[1].count = 0;  memset(s_buf[1].entries, 0, sizeof(s_buf[1].entries));
    Config::SetEspLastError("Cache cleared \xe2\x80\x94 rescanning next frame");
}

// ── Helpers ───────────────────────────────────────────────────────────────────
static void SetLog(const char* fmt, ...)
{
    char tmp[256];
    va_list va;
    va_start(va, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, va);
    va_end(va);
    Config::SetEspLastError(tmp);
}

void SwapEspBuffers()
{
    std::lock_guard<std::mutex> lk(s_swapMtx);
    s_writeIdx ^= 1;
}

// Bug #3 fix: return by value so callers get a stable snapshot.
// The mutex protects only the index read; returning a reference
// would let consumers read while the writer overwrites.
EspBuffer GetFrontBuffer()
{
    std::lock_guard<std::mutex> lk(s_swapMtx);
    return s_buf[s_writeIdx ^ 1];
}

// ── WorldToScreen ─────────────────────────────────────────────────────────────
Vector2 WorldToScreen(const Matrix4x4& vm, const Vector3& pos, int width, int height)
{
    Vector2 result(-1.f, -1.f);

    float v9  = pos.x * vm.m00 + pos.y * vm.m10 + pos.z * vm.m20 + vm.m30;
    float v10 = pos.x * vm.m01 + pos.y * vm.m11 + pos.z * vm.m21 + vm.m31;
    float v12 = pos.x * vm.m03 + pos.y * vm.m13 + pos.z * vm.m23 + vm.m33;

    if (v12 < 0.001f) return result;

    float halfW = width  * 0.5f;
    float halfH = height * 0.5f;
    result.x = halfW + (halfW * v9)  / v12;
    result.y = halfH - (halfH * v10) / v12;
    return result;
}

// ── Bone projection ───────────────────────────────────────────────────────────
static BonePos ProjectBone(uint32_t boneHandle, const Matrix4x4& vm, int w, int h)
{
    BonePos bp{};
    if (boneHandle == 0) return bp;

    Vector3 world{};
    if (!GetNodePosition(boneHandle, world)) return bp;
    if (world.IsZero()) return bp;

    Vector2 sc = WorldToScreen(vm, world, w, h);
    if (sc.x < 0.f && sc.y < 0.f) return bp;

    const float kM = (float)std::max(w, h) * 2.f;
    if (sc.x < -kM || sc.x > (float)w + kM || sc.y < -kM || sc.y > (float)h + kM)
        return bp;

    bp.screen = sc;
    bp.valid  = true;
    return bp;
}

// RetryRead — only for the root pointer chain (4 pointers).
// NEVER use inside the entity loop; the Sleep() would block for ~900 ms/frame
// with 60+ entities and is the root cause of D:0 in earlier builds.
// Bug #9 note: T must support operator!= and value-init T{} (e.g. uint32_t).
// Do NOT instantiate with Vector3/Matrix4x4/struct types.
template<typename T>
static bool RetryRead(uintptr_t addr, T& out, int maxTries = 3)
{
    for (int i = 0; i < maxTries; ++i)
    {
        if (ReadZ(addr, out) && out != T{}) return true;
        Sleep(5);
    }
    return false;
}

// Blank back buffer, publish, return.
#define BAIL(msg, ...) \
    do { \
        back.count = 0; \
        SetLog(msg, ##__VA_ARGS__); \
        SwapEspBuffers(); \
        return; \
    } while(0)

// Track a GVA value across frames; flush physical + entity cache when it changes.
static bool TrackValue(uint32_t& stored, uint32_t current)
{
    if (current == stored) return false;
    if (stored != 0) {
        InvalidatePhysCache();
        s_entityCache.clear();   // stale entity addresses from old mode
    }
    stored = current;
    return true;
}

// ── UpdateEspData ─────────────────────────────────────────────────────────────
void UpdateEspData(int emulatorW, int emulatorH)
{
    if (Config::ClearCacheRequested.exchange(false))
        ResetEspState();

    // Reset ESP — clears entity classification cache and forces full re-enumeration
    if (Config::ResetEspRequested.exchange(false))
    {
        ResetEspState();
        Config::SetEspLastError("ESP Reset \xe2\x80\x94 re-counting all enemies");
    }

    EspBuffer& back = s_buf[s_writeIdx];
    back.count = 0;
    memset(back.entries, 0, sizeof(back.entries));

    // Reset pipeline flags
    Config::StageIl2Cpp.store(false);
    Config::StageConvert.store(false);
    Config::StageGameFacade.store(false);
    Config::StageCurrentGame.store(false);
    Config::StageMatch.store(false);
    Config::StageLocalPlayer.store(false);
    Config::StageViewMatrix.store(false);
    Config::StageEntities.store(false);
    Config::EspEntityCount.store(0);
    Config::EspDrawnCount.store(0);
    Config::LocalPlayerDead.store(false);

    // ── Stage 0: hooks ────────────────────────────────────────────────────────
    if (!Config::HookReady.load())
        BAIL("Hooks not ready — run as Administrator and restart BlueStacks");

    // ── Stage 1: il2cpp base ──────────────────────────────────────────────────
    uintptr_t il2cpp = Config::Il2CppBase.load();
    if (il2cpp == 0)
        BAIL("il2cpp=0 — open Free Fire inside BlueStacks then press Start Setup");
    Config::StageIl2Cpp.store(true);

    // ── Stage 2: ConvertZ sanity probe ────────────────────────────────────────
    uintptr_t initAddr = il2cpp + Offsets::InitBase;
    {
        bool ok = false;
        uint32_t probe = 0;
        for (int t = 0; t < 10 && !ok; ++t)
        {
            uintptr_t phys = 0;
            if (ConvertZ(initAddr, phys) && phys != 0 && PhysRead(phys, &probe, sizeof(probe)))
                ok = true;
            else
                Sleep(5u * (1u << std::min(t, 4)));
        }
        if (!ok)
            BAIL("ConvertZ fail — memory read error, try re-injecting the DLL");
    }
    Config::StageConvert.store(true);

    // ── Stage 3: root pointer chain ───────────────────────────────────────────
    uint32_t baseGF = 0, gameFacade = 0, staticGF = 0;
    if (!RetryRead(initAddr, baseGF) || baseGF == 0)
        BAIL("BaseGameFacade=0 — wait for Free Fire to finish loading");
    if (!RetryRead(baseGF, gameFacade) || gameFacade == 0)
        BAIL("GameFacade=0 — wait for Free Fire lobby to fully load");
    Config::StageGameFacade.store(true);

    if (!RetryRead(gameFacade + (uint32_t)Offsets::StaticClass, staticGF) || staticGF == 0)
        BAIL("StaticClass=0 — game state error, try resetting ESP");

    uint32_t currentGame = 0;
    if (!RetryRead(staticGF, currentGame) || currentGame == 0)
        BAIL("currentGame=0 — you are on the main menu, open Free Fire and enter the lobby");
    Config::StageCurrentGame.store(true);

    TrackValue(s_lastCurrentGame, currentGame);

    // Bug #2 fix: cap entity cache size to prevent unbounded growth
    if (s_entityCache.size() > 2000) {
        s_entityCache.clear();
    }

    // ── currentMatch ──────────────────────────────────────────────────────────
    uint32_t currentMatch = 0;
    if (!ReadZ(currentGame + (uint32_t)Offsets::CurrentMatch, currentMatch) || currentMatch == 0)
    {
        if (s_wasInMatch) {
            InvalidatePhysCache();
            s_entityCache.clear();
            s_wasInMatch        = false;
            s_lastCurrentMatch  = 0;
            s_lastEntityDict    = 0;
            s_lastLocalPlayer   = 0;
            s_matchStableFrames = 0;
        }
        BAIL("Match=0 — not in a match yet, join a game first");
    }
    Config::StageMatch.store(true);

    {
        uint32_t matchStatus = 0;
        ReadZ(currentMatch + (uint32_t)Offsets::MatchStatus, matchStatus);
        if (matchStatus == 0)
            BAIL("Match loading — waiting for the round to start");
    }

    bool newRound = TrackValue(s_lastCurrentMatch, currentMatch);
    if (newRound || !s_wasInMatch) {
        if (!newRound) { InvalidatePhysCache(); s_entityCache.clear(); }
        s_wasInMatch        = true;
        s_matchStableFrames = 0;
    }

    if (s_matchStableFrames < SETTLE_FRAMES) {
        ++s_matchStableFrames;
        BAIL("Settling (%d/%d)", s_matchStableFrames, SETTLE_FRAMES);
    }

    // ── Local player — savedLocalPlayer cache ─────────────────────────────────
    uint32_t localPlayer = 0;
    if (!ReadZ(currentMatch + (uint32_t)Offsets::LocalPlayer, localPlayer) || localPlayer == 0) {
        if (s_savedLocalPlayer == 0)
            BAIL("LocalPlayer=0");
        localPlayer = s_savedLocalPlayer;
    }
    else {
        if (localPlayer != s_savedLocalPlayer && s_savedLocalPlayer != 0) {
            InvalidatePhysCache();
            s_entityCache.clear();
        }
        s_savedLocalPlayer = localPlayer;
        s_lastLocalPlayer  = localPlayer;
        Config::LocalPlayerAddr.store(localPlayer);  // shared with SilentAim thread
    }

    if (localPlayer < 0x10000)
        BAIL("LocalPlayer garbage ptr 0x%08X", localPlayer);
    Config::StageLocalPlayer.store(true);

    {
        bool dead = false;
        ReadZ(localPlayer + (uint32_t)Offsets::Player_IsDead, dead);
        Config::LocalPlayerDead.store(dead);
        // Don't bail — keep drawing ESP during Lone Wolf / CS spectate view
    }

    // ── Stage 4: LocalPos from MainCameraTransform ────────────────────────────
    // Reference code: localPlayer + 0x254 → mainTransform → Transform.GetPosition
    // This gives the camera world position used for distance calculations.
    // Much more reliable than the Player_Data pointer chain (which produced
    // garbage floats and caused all entities to be distance-filtered in prior builds).
    Vector3 localPos  = Vector3::Zero();
    bool    localPosValid = false;
    {
        uint32_t mainTransform = 0;
        if (ReadZ(localPlayer + (uint32_t)Offsets::MainCameraTransform, mainTransform)
            && mainTransform != 0)
        {
            if (GetNodePosition(mainTransform, localPos) && !localPos.IsZero())
                localPosValid = true;
        }
        // Fallback: Root bone (reliable but slightly different origin than camera)
        if (!localPosValid) {
            uint32_t rootBone = 0;
            if (ReadZ(localPlayer + (uint32_t)Offsets::Root, rootBone) && rootBone != 0)
                localPosValid = GetNodePosition(rootBone, localPos) && !localPos.IsZero();
        }
        // If both fail: localPosValid=false → distance filter bypassed below
    }

    // ── Stage 5: View matrix ──────────────────────────────────────────────────
    Matrix4x4 viewMatrix{};
    {
        uint32_t followCam = 0, cam = 0, camBase = 0;
        bool ok =
            ReadZ(localPlayer + (uint32_t)Offsets::FollowCamera, followCam) && followCam != 0 &&
            ReadZ(followCam   + (uint32_t)Offsets::Camera,       cam)       && cam       != 0 &&
            ReadZ(cam + 0x8,                                      camBase)   && camBase   != 0 &&
            ReadZ(camBase + (uint32_t)Offsets::ViewMatrix,        viewMatrix);
        if (ok) {
            s_savedViewMatrix = viewMatrix;
            s_hasViewMatrix   = true;
        }
        else if (s_hasViewMatrix) {
            viewMatrix = s_savedViewMatrix;
        }
        else {
            BAIL("ViewMatrix fail");
        }
        Config::StageViewMatrix.store(true);
    }

    // ── Stage 6: Entity dictionary ────────────────────────────────────────────
    // Reference GetEntities(): dict + 0x10 = count, dict + 0x0C = entries ptr,
    // entries + 0x10 = first slot, stride 0x10, entity at slot + 0x0C.
    uint32_t entityDict = 0;
    if (!ReadZ(currentGame + (uint32_t)Offsets::DictionaryEntities, entityDict) || entityDict == 0)
        BAIL("DictionaryEntities=0");

    // Track entityDict VALUE — flush when the game swaps the dictionary object
    // (happens on every game-mode transition; same GVA now maps to new physical page)
    if (entityDict != s_lastEntityDict) {
        if (s_lastEntityDict != 0) { InvalidatePhysCache(); s_entityCache.clear(); }
        s_lastEntityDict = entityDict;
    }

    int32_t dictCount = 0;
    if (!ReadZ(entityDict + 0x10, dictCount) || dictCount < 1 || dictCount > 10000)
        BAIL("DictCount=%d (bad)", dictCount);

    uint32_t entriesObj = 0;
    if (!ReadZ(entityDict + 0x0C, entriesObj) || entriesObj == 0)
        BAIL("Entries ptr=0");

    const uint32_t entryBase = entriesObj + 0x10;
    const uint32_t loopCap   = std::min((uint32_t)dictCount, (uint32_t)600);
    Config::EspEntityCount.store((uint32_t)dictCount);
    Config::StageEntities.store(true);

    const bool wantSkel    = Config::EspSkeletonEnabled.load();
    const bool wantName    = Config::EspNameEnabled.load();
    const bool wantHealth  = Config::EspHealthEnabled.load() || Config::EspBoxEnabled.load();
    const bool wantWeapon  = Config::EspWeaponEnabled.load();
    const float maxDist    = (float)Config::MaxDistance.load();

    const float marginX = (float)emulatorW * 0.50f;
    const float marginY = (float)emulatorH * 0.50f;

    int sNull = 0, sDead = 0, sTeam = 0, sLocal = 0;
    int sUnknown = 0, sNew = 0, sHead = 0, sDist = 0, sOffscr = 0;
    int drawn = 0;

    // ── Stage 7: Entity loop — Bool3 cache (reference code logic) ─────────────
    for (uint32_t i = 0; i < loopCap && drawn < MAX_ENTITIES; ++i)
    {
        const uint32_t slot = entryBase + i * (uint32_t)Offsets::EntityStride;

        // Skip freed / tombstone slots (hashCode < 0 in C# Dictionary)
        int32_t hashCode = 0;
        ReadZ(slot + 0x00, hashCode);
        if (hashCode < 0) { ++sNull; continue; }

        uint32_t entity = 0;
        if (!ReadZ(slot + (uint32_t)Offsets::EntityPtrOff, entity) || entity == 0)
        { ++sNull; continue; }
        if (entity < 0x10000) { ++sNull; continue; }
        if (entity == localPlayer) { ++sLocal; continue; }

        // ── Bool3 entity cache (mirrors Core.Entities in reference code) ──────
        // NEW entities: insert with Unknown/false; defer real work to next frame.
        // Known-team entities: skip immediately (Bool3::True).
        // Known-enemy entities (IsKnown=true): fall through to rendering.
        auto it = s_entityCache.find(entity);
        if (it == s_entityCache.end()) {
            s_entityCache.emplace(entity, CachedEntity{});
            ++sNew;
            continue;   // process next frame once inserted
        }
        CachedEntity& ce = it->second;

        if (ce.isTeam == Bool3::True) { ++sTeam; continue; }

        // ── Team resolution (runs once per entity per visibility window) ──────
        // Mirrors reference code: only commit when isVisible == true.
        // Once IsTeam is False and IsKnown is set, this block is never entered.
        if (ce.isTeam == Bool3::Unknown) {
            uint32_t avatarMgr = 0, avatar = 0, avatarData = 0;
            if (ReadZ(entity    + (uint32_t)Offsets::AvatarManager, avatarMgr) && avatarMgr != 0 &&
                ReadZ(avatarMgr + (uint32_t)Offsets::Avatar,        avatar)    && avatar    != 0)
            {
                bool isVisible = false;
                ReadZ(avatar + (uint32_t)Offsets::Avatar_IsVisible, isVisible);
                if (isVisible) {
                    if (ReadZ(avatar + (uint32_t)Offsets::Avatar_Data, avatarData) && avatarData != 0) {
                        bool isTeamFlag = false;
                        if (ReadZ(avatarData + (uint32_t)Offsets::Avatar_Data_IsTeam, isTeamFlag)) {
                            if (isTeamFlag) {
                                ce.isTeam  = Bool3::True;
                                ++sTeam;
                                continue;
                            }
                            else {
                                ce.isTeam  = Bool3::False;
                                ce.isKnown = true;
                            }
                        }
                    }
                }
            }
        }

        if (!ce.isKnown) { ++sUnknown; continue; }

        // ── Per-frame: dead check ─────────────────────────────────────────────
        bool isDead = false;
        ReadZ(entity + (uint32_t)Offsets::Player_IsDead, isDead);
        if (isDead) { ++sDead; continue; }

        // ── Knocked status ────────────────────────────────────────────────────
        bool isKnocked = false;
        {
            uint32_t sb = 0;
            if (ReadZ(entity + (uint32_t)Offsets::Player_ShadowBase, sb) && sb != 0) {
                int xpose = 0;
                if (ReadZ(sb + (uint32_t)Offsets::XPose, xpose) && xpose == 8)
                    isKnocked = true;
            }
        }

        // ── Head bone → world position ────────────────────────────────────────
        uint32_t headBone = 0;
        if (!ReadZ(entity + (uint32_t)Offsets::Head, headBone) || headBone == 0)
        { ++sHead; continue; }
        Vector3 headWorld{};
        if (!GetNodePosition(headBone, headWorld) || headWorld.IsZero())
        { ++sHead; continue; }

        // ── Distance filter ───────────────────────────────────────────────────
        // Reference: Vector3.Distance(Core.LocalMainCamera, entity.Head)
        // Only applied when localPos read succeeded.  If it failed, we bypass
        // the filter entirely so no entity is wrongly eliminated.
        float dist = 0.f;
        if (localPosValid) {
            dist = Vector3::Distance(localPos, headWorld);
            if (dist > maxDist) { ++sDist; continue; }
        }

        // ── Screen-space projection ───────────────────────────────────────────
        Vector2 headSc = WorldToScreen(viewMatrix, headWorld, emulatorW, emulatorH);
        // Sentinel check: WorldToScreen returns (-1,-1) for behind-camera entities
        if (headSc.x < 0.f && headSc.y < 0.f) { ++sOffscr; continue; }
        if (headSc.x < -marginX || headSc.x > (float)emulatorW + marginX ||
            headSc.y < -marginY || headSc.y > (float)emulatorH + marginY)
        { ++sOffscr; continue; }

        // ── Root bone ─────────────────────────────────────────────────────────
        uint32_t rootBone = 0;
        Vector3  rootWorld{};
        if (ReadZ(entity + (uint32_t)Offsets::Root, rootBone) && rootBone != 0)
            GetNodePosition(rootBone, rootWorld);

        // Bug #5 fix: scale root fallback by distance instead of fixed 50px.
        // Close enemies (~10m) get ~60px, far enemies (~300m) get ~15px.
        float rootFallbackPx = 50.f;
        if (dist > 1.f) {
            rootFallbackPx = 800.f / dist;   // inverse proportional
            rootFallbackPx = std::max(15.f, std::min(80.f, rootFallbackPx));
        }

        Vector2 rootSc = rootWorld.IsZero()
            ? Vector2{ headSc.x, headSc.y + rootFallbackPx }
            : WorldToScreen(viewMatrix, rootWorld, emulatorW, emulatorH);

        // ── Commit to back buffer ─────────────────────────────────────────────
        EspEntry& e  = back.entries[drawn++];
        e.headScreen = headSc;
        e.rootScreen = rootSc;
        e.isKnocked  = isKnocked;
        e.distanceM  = dist;
        e.valid      = true;
        e.headWorld  = headWorld;
        e.entityAddr = entity;   // stored for AimVisible collider writes

        // Neck + hip world positions — always read for aimbot target selection.
        // These run outside the wantSkel block so aimbot works even with
        // skeleton-ESP toggled off.
        {
            uint32_t b = 0;
            Vector3 nw{}, hw{};
            if (ReadZ(entity + (uint32_t)Offsets::Neck, b) && b != 0)
                GetNodePosition(b, nw);
            e.neckWorld = nw;

            b = 0;
            if (ReadZ(entity + (uint32_t)Offsets::Hip, b) && b != 0)
                GetNodePosition(b, hw);
            e.hipWorld = hw;
        }

        // ── Health — pool chain from reference code ───────────────────────────
        // entity + Player_Data(0x48) → dp → dp + 0x8 → poolObj
        // → poolObj + 0x10 → pool → pool + 0x10 → Health (short)
        if (wantHealth) {
            e.health    = 0.f;
            e.maxHealth = 200.f;
            uint32_t dp = 0, poolObj = 0, pool = 0;
            if (ReadZ(entity + (uint32_t)Offsets::Player_Data, dp) && dp != 0 &&
                ReadZ(dp + 0x8,  poolObj) && poolObj != 0 &&
                ReadZ(poolObj + 0x10, pool) && pool != 0)
            {
                int16_t hp = 0;
                if (ReadZ(pool + 0x10, hp) && hp > 0)
                    e.health = (float)hp;
            }
        }

        // ── Weapon name — pool chain from reference Data.cs ──────────────────
        // entity + Player_Data(0x48) → dp → dp+0x8 → poolObj
        // → poolObj + 0x20 → pool → pool + 0x10 → weaponId (int16)
        // Note: weapon uses poolObj+0x20 (health uses poolObj+0x10).
        //
        // BUG FIX (bots): old code used weaponId > 0, which silently skipped
        // bot AK47 (id=0).  Changed to weaponId >= 0 so bots show their weapon.
        // FIST (id=1) is now included — bots holding nothing show "{-_-} FIST".
        if (wantWeapon) {
            e.weaponName[0] = '\0';
            uint32_t dp = 0, poolObj = 0, pool = 0;
            if (ReadZ(entity + (uint32_t)Offsets::Player_Data, dp) && dp != 0 &&
                ReadZ(dp + 0x8,   poolObj) && poolObj != 0 &&
                ReadZ(poolObj + 0x20, pool) && pool != 0)
            {
                int16_t weaponId = 0;
                // weaponId >= 0 catches ID=0 (AK47 bot) that > 0 previously missed.
                // Negative values are garbage reads — ignore them.
                if (ReadZ(pool + 0x10, weaponId) && weaponId >= 0)
                {
                    const char* wn  = WeaponIndex::GetWeaponName((int)weaponId);
                    const char* art = WeaponIndex::GetWeaponAscii((int)weaponId);
                    if (wn)
                    {
                        // Combine ASCII art icon + space + weapon name into one string.
                        // Rendered as a single centered line below the distance label.
                        // e.g.  "--[=|=]--> AK47"
                        if (art)
                            snprintf(e.weaponName, sizeof(e.weaponName),
                                     "%s %s", art, wn);
                        else
                            strncpy_s(e.weaponName, wn, _TRUNCATE);
                    }
                }
            }
        }

        // ── Player name (labels bots when name is empty) ──────────────────────
        if (wantName) {
            uint32_t namePtr = 0;
            bool hasName = false;
            if (ReadZ(entity + (uint32_t)Offsets::Player_Name, namePtr) && namePtr != 0)
            {
                std::string n = ReadPlayerName(namePtr);
                if (!n.empty()) {
                    strncpy_s(e.name, n.c_str(), _TRUNCATE);
                    hasName = true;
                }
            }
            // Bots have null or empty names — label them so ESP Names shows "BOT"
            if (!hasName) {
                strncpy_s(e.name, "[BOT]", _TRUNCATE);
            }
        }

        // ── Skeleton bones ────────────────────────────────────────────────────
        // b is reset to 0 before every read so a failed ReadZ never
        // passes a stale handle from the previous bone to ProjectBone.
        if (wantSkel) {
            uint32_t b = 0;
            b = 0; ReadZ(entity + (uint32_t)Offsets::Neck,          b); e.neck      = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::Spine,         b); e.spine     = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::Hip,           b); e.hip       = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::LeftShoulder,  b); e.lShoulder = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::RightShoulder, b); e.rShoulder = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::LeftElbow,     b); e.lElbow    = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::RightElbow,    b); e.rElbow    = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::LeftWrist,     b); e.lWrist    = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::RightWrist,    b); e.rWrist    = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::LeftCalf,      b); e.lKnee     = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::RightCalf,     b); e.rKnee     = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::LeftFoot,      b); e.lFoot     = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
            b = 0; ReadZ(entity + (uint32_t)Offsets::RightFoot,     b); e.rFoot     = ProjectBone(b, viewMatrix, emulatorW, emulatorH);
        }
    }

    back.count = drawn;
    Config::EspDrawnCount.store((uint32_t)drawn);

    SetLog("drawn=%d  cap=%d  scan=%u  localOK=%d\n"
           "skip null=%d new=%d unknown=%d team=%d local=%d\n"
           "     dead=%d head=%d dist=%d offscr=%d  cache=%zu",
           drawn, dictCount, loopCap, (int)localPosValid,
           sNull, sNew, sUnknown, sTeam, sLocal,
           sDead, sHead, sDist, sOffscr, s_entityCache.size());

    SwapEspBuffers();

    // ── Aimbot pass ───────────────────────────────────────────────────────────
    // Always attempt — even when localPos read failed this frame.
    // Aimbot itself guards against a zero camera position.
    if (localPlayer != 0)
        RunAimbot(localPlayer, localPos, viewMatrix, emulatorW, emulatorH);

    // ── AimVisible pass ───────────────────────────────────────────────────────
    // No keybind — active whenever Config::AimVisibleEnabled is true.
    // Writes head collider into enemy LegitCollider slot so the engine
    // auto-locks bullets onto the head (reference: AimbotVisible.cs).
    RunAimVisible(viewMatrix, localPos, emulatorW, emulatorH);

    // SilentAim runs on its own dedicated thread — see dllmain.cpp / silentaim.cpp.
    // Removed from here so it can poll at ~1 kHz instead of the 30 Hz ESP rate.
}
