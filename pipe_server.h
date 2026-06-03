#pragma once

// Start the named-pipe server thread (\\.\pipe\esp_pipe).
// Call once from MainThread before RunOverlay().
void StartPipeServer();

// Signal the pipe server to stop and wait for it to exit.
// Call from DLL_PROCESS_DETACH / after RunOverlay() returns.
void StopPipeServer();

// PipeLog — intentional no-op.
// All unsolicited pipe output is suppressed: only command responses and
// setup step messages (from setup.cpp via PipeWrite) reach the client.
// The declaration is kept so existing callers (adb.cpp, overlay.cpp) compile
// without modification.
inline void PipeLog(const char*) {}

// Thread-safe write of "msg\n" to the pipe handle h.
// Acquires the shared write critical section so setup.cpp step messages
// never interleave with command replies written by the pipe-server thread.
// Call only while h is a valid, open pipe handle.
void PipeWrite(HANDLE h, const char* msg);
