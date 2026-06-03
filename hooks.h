#pragma once
#include <cstdint>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// ============================================================
//  BstkVMM hooks (reference EspGame.h — __cdecl, 4-arg PhysRead)
//  MinHook sources: hook.c, buffer.c, trampoline.c, hde64/32.c
// ============================================================

typedef int  (__cdecl* PGMPhysReadFunc)            (void* pVM, uintptr_t GCPhys, void* pvBuf, size_t cbRead);
typedef int  (__cdecl* PGMPhysSimpleWriteGCPhysFunc)(void* pVM, uintptr_t GCPhys, void* pvBuf, size_t cbWrite);
typedef int  (__cdecl* PGMPhysGCPtr2GCPhysFunc)    (void* pCPU, uintptr_t GCPtr, uintptr_t* pGCPhys);
typedef void*(__cdecl* VMMGetCpuByIdFunc)           (void* pVM, int idCpu);

extern void*                        g_vmPtr;
extern PGMPhysReadFunc              ogPhysRead;
extern PGMPhysSimpleWriteGCPhysFunc ogWrite;
extern PGMPhysGCPtr2GCPhysFunc      ogCast;
extern VMMGetCpuByIdFunc            ogCPU;

bool InitHooks();
void ShutdownHooks();

// Guest virtual -> host physical (reference ConvertZ)
// TTL-based cache: re-translates stale entries automatically
bool ConvertZ(uintptr_t guestVA, uintptr_t& outPhys);

// Invalidate the entire GVA->GPA cache (call after game restart detected)
void InvalidatePhysCache();

bool PhysRead (uintptr_t phys, void* buf, size_t sz);
bool PhysWrite(uintptr_t phys, const void* buf, size_t sz);
bool GuestRead (uintptr_t guestVA, void* buf, size_t sz);
bool GuestWrite(uintptr_t guestVA, const void* buf, size_t sz);

// Quick self-test: can we read 4 bytes at guestVA?
bool TestGuestRead(uintptr_t guestVA);
