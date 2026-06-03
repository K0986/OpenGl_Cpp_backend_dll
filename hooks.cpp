#include "config.h"
#include "hooks.h"
#include <MinHook.h>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>

void*                        g_vmPtr    = nullptr;
PGMPhysReadFunc              ogPhysRead = nullptr;
PGMPhysSimpleWriteGCPhysFunc ogWrite    = nullptr;
PGMPhysGCPtr2GCPhysFunc      ogCast     = nullptr;
VMMGetCpuByIdFunc            ogCPU      = nullptr;

// ============================================================
//  GVA -> GPA translation cache
//
//  Uses std::shared_mutex so multiple ESP-thread reads can
//  happen concurrently without contention.
//  TTL = 300 ms (matching EspGame.h's zTimeMs = 300).
//  Previously 400 ms — shorter means stale round data never
//  lingers past ~1/3 of a second.
//
//  Stale fallback logic (from EspGame.h ConvertZ):
//    1. Try to get a fresh physical address (2 vCPUs, fast).
//    2. If fresh translate fails, return old cached value.
//    3. This lets the ESP continue drawing for one frame even
//       when a round transition briefly breaks page tables.
// ============================================================
struct CacheEntry
{
    uintptr_t phys;
    std::chrono::steady_clock::time_point cachedAt;
};

static constexpr auto CACHE_TTL = std::chrono::milliseconds(300);
static constexpr int  MAX_VCPU  = 4;   // BlueStacks typically 2-4 vCPUs

static std::unordered_map<uintptr_t, CacheEntry> g_physCache;
static std::shared_mutex                          g_cacheMtx;

// ============================================================
//  HookedPGMPhysRead — captures pVM on first call
// ============================================================
static int __cdecl HookedPGMPhysRead(void* pVM, uintptr_t GCPhys,
                                     void* pvBuf, size_t cbRead)
{
    if (!g_vmPtr) g_vmPtr = pVM;
    return ogPhysRead(pVM, GCPhys, pvBuf, cbRead);
}

// ============================================================
//  ConvertZ — GVA → GPA  (mirrors EspGame.h ConvertZ logic)
//
//  Order:
//    1. Check cache under shared_lock (read-only, no contention)
//    2. On stale/miss: try fresh translate (exclusive lock only
//       when writing)
//    3. If fresh fails: use stale value (EspGame.h behaviour)
// ============================================================
bool ConvertZ(uintptr_t address, uintptr_t& phys)
{
    phys = 0;
    if (!g_vmPtr || !ogCPU || !ogCast) return false;

    auto now = std::chrono::steady_clock::now();

    // ── 1. Check cache (shared / read lock) ──────────────────
    uintptr_t stalePhys = 0;
    bool      haveStale = false;
    {
        std::shared_lock<std::shared_mutex> lk(g_cacheMtx);
        auto it = g_physCache.find(address);
        if (it != g_physCache.end())
        {
            bool fresh = (now - it->second.cachedAt) < CACHE_TTL;
            if (fresh)
            {
                phys = it->second.phys;
                return true;    // fast path — no exclusive lock needed
            }
            stalePhys = it->second.phys;
            haveStale = true;
        }
    }

    // ── 2. Stale or miss: attempt fresh translate ─────────────
    // Only try 2 vCPUs (like EspGame.h) — covers all BlueStacks
    // configs.  Trying all 8 when most setups only have 2 just
    // wastes time per cache miss.
    for (int cpuId = 0; cpuId < MAX_VCPU; ++cpuId)
    {
        void* cpu = ogCPU(g_vmPtr, cpuId);
        if (!cpu) continue;

        uintptr_t newPhys = 0;
        if (ogCast(cpu, address, &newPhys) == 0 && newPhys != 0)
        {
            std::unique_lock<std::shared_mutex> lk(g_cacheMtx);
            g_physCache[address] = { newPhys, now };
            // Bug #2 fix: cap cache size to prevent unbounded growth / OOM
            if (g_physCache.size() > 50000) {
                // Evict entries older than 2x TTL
                auto cutoff = now - CACHE_TTL * 2;
                for (auto it2 = g_physCache.begin(); it2 != g_physCache.end(); ) {
                    if (it2->second.cachedAt < cutoff)
                        it2 = g_physCache.erase(it2);
                    else
                        ++it2;
                }
            }
            phys = newPhys;
            return true;
        }
    }

    // ── 3. Fresh translate failed — use stale (EspGame.h style)
    if (haveStale)
    {
        phys = stalePhys;
        return true;
    }
    return false;
}

// ============================================================
//  InvalidatePhysCache — wipe entire cache
//  Call on new match / round start.  Use sparingly — wiping
//  mid-scan causes every subsequent ConvertZ to be a miss and
//  a fresh vCPU translate, which is 4× slower than a cache hit.
// ============================================================
void InvalidatePhysCache()
{
    std::unique_lock<std::shared_mutex> lk(g_cacheMtx);
    g_physCache.clear();
}

// ============================================================
//  Physical / Guest I/O
// ============================================================
bool PhysRead(uintptr_t phys, void* buf, size_t sz)
{
    if (!ogPhysRead || !g_vmPtr || phys == 0) return false;
    return ogPhysRead(g_vmPtr, phys, buf, sz) == 0;
}

bool PhysWrite(uintptr_t phys, const void* buf, size_t sz)
{
    if (!ogWrite || !g_vmPtr || phys == 0) return false;
    return ogWrite(g_vmPtr, phys, const_cast<void*>(buf), sz) == 0;
}

bool GuestRead(uintptr_t guestVA, void* buf, size_t sz)
{
    if (sz == 0) return true;
    // Handle reads that cross a 4 KB page boundary
    const uintptr_t pageEnd = (guestVA | 0xFFF) + 1;
    if (guestVA + sz > pageEnd)
    {
        const size_t first = (size_t)(pageEnd - guestVA);
        uintptr_t p1 = 0;
        if (!ConvertZ(guestVA, p1) || p1 == 0) return false;
        if (!PhysRead(p1, buf, first)) return false;

        uintptr_t p2 = 0;
        if (!ConvertZ(pageEnd, p2) || p2 == 0) return false;
        return PhysRead(p2, (char*)buf + first, sz - first);
    }
    uintptr_t p = 0;
    if (!ConvertZ(guestVA, p) || p == 0) return false;
    return PhysRead(p, buf, sz);
}

bool GuestWrite(uintptr_t guestVA, const void* buf, size_t sz)
{
    if (sz == 0) return true;
    // Handle writes that cross a 4 KB page boundary
    const uintptr_t pageEnd = (guestVA | 0xFFF) + 1;
    if (guestVA + sz > pageEnd)
    {
        const size_t first = (size_t)(pageEnd - guestVA);
        uintptr_t p1 = 0;
        if (!ConvertZ(guestVA, p1) || p1 == 0) return false;
        if (!PhysWrite(p1, buf, first)) return false;

        uintptr_t p2 = 0;
        if (!ConvertZ(pageEnd, p2) || p2 == 0) return false;
        return PhysWrite(p2, (const char*)buf + first, sz - first);
    }
    uintptr_t p = 0;
    if (!ConvertZ(guestVA, p) || p == 0) return false;
    return PhysWrite(p, buf, sz);
}

bool TestGuestRead(uintptr_t guestVA)
{
    uint32_t probe = 0;
    return GuestRead(guestVA, &probe, sizeof(probe));
}

// ============================================================
//  InitHooks — install MinHook on BstkVMM!PGMPhysRead
// ============================================================
bool InitHooks()
{
    if (Config::HookReady.load()) return true;

    HMODULE vmm = GetModuleHandleA("BstkVMM.dll");
    if (!vmm) return false;

    auto readFunc = (PGMPhysReadFunc)GetProcAddress(vmm, "PGMPhysRead");
    if (!readFunc) return false;

    if (MH_Initialize() != MH_OK) return false;

    if (MH_CreateHook((LPVOID)readFunc, (LPVOID)&HookedPGMPhysRead,
                      (LPVOID*)&ogPhysRead) != MH_OK)
        return false;

    if (MH_EnableHook((LPVOID)readFunc) != MH_OK)
        return false;

    // Wait up to 2 seconds for the first PhysRead call to arrive
    for (int i = 0; i < 200 && g_vmPtr == nullptr; ++i)
        Sleep(10);

    if (!g_vmPtr) return false;

    ogCPU   = (VMMGetCpuByIdFunc)           GetProcAddress(vmm, "VMMGetCpuById");
    ogCast  = (PGMPhysGCPtr2GCPhysFunc)     GetProcAddress(vmm, "PGMPhysGCPtr2GCPhys");
    ogWrite = (PGMPhysSimpleWriteGCPhysFunc)GetProcAddress(vmm, "PGMPhysSimpleWriteGCPhys");

    if (!ogCPU || !ogCast || !ogWrite) return false;

    Config::HookReady.store(true);
    return true;
}

void ShutdownHooks()
{
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_vmPtr = nullptr;
    InvalidatePhysCache();
    Config::HookReady.store(false);
}
