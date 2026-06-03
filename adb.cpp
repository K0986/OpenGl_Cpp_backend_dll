#include "config.h"
  #include "adb.h"
  #include "overlay.h"
  #include "hooks.h"
  #include "pipe_server.h"
  #include <TlHelp32.h>
  #include <string>
  #include <sstream>
  #include <vector>
  #include <thread>
  #include <chrono>
  #include <algorithm>
  #include <atomic>
  #include <mutex>

  std::atomic<bool> g_adbThreadRunning{ false };
  std::atomic<bool> g_adbInitialized{ false };

  // Named mutex name — shared across all processes so two DLL instances
  // injected into different HD-Player.exe windows never race on the ADB server.
  static const wchar_t* ADB_INIT_MUTEX = L"Global\\SupremoAdbInitMtx";

  // ============================================================
  //  WaitForProcessesDead
  // ============================================================
  static bool WaitForProcessesDead(const wchar_t* const* targets, DWORD timeoutMs = 2000)
  {
      ULONGLONG start = GetTickCount64();
      while ((GetTickCount64() - start) < timeoutMs)
      {
          HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
          if (snap == INVALID_HANDLE_VALUE) { Sleep(50); continue; }

          bool anyAlive = false;
          PROCESSENTRY32 pe{};
          pe.dwSize = sizeof(pe);

          if (Process32First(snap, &pe))
          {
              do {
                  for (int i = 0; targets[i]; ++i)
                  {
                      if (_wcsicmp(pe.szExeFile, targets[i]) == 0)
                      {
                          anyAlive = true;
                          break;
                      }
                  }
                  if (anyAlive) break;
              } while (Process32Next(snap, &pe));
          }
          CloseHandle(snap);

          if (!anyAlive) return true;
          Sleep(50);
      }
      return false;
  }

  // ============================================================
  //  KillAdbZ
  // ============================================================
  void KillAdbZ()
  {
      const wchar_t* targets[] = {
          L"adb.exe", L"HD-Adb.exe", L"BstkSVC.exe", nullptr
      };

      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (snap != INVALID_HANDLE_VALUE)
      {
          PROCESSENTRY32 pe{};
          pe.dwSize = sizeof(pe);
          if (Process32First(snap, &pe))
          {
              do {
                  for (int i = 0; targets[i]; ++i)
                  {
                      if (_wcsicmp(pe.szExeFile, targets[i]) == 0)
                      {
                          HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                          if (h) { TerminateProcess(h, 0); CloseHandle(h); }
                      }
                  }
              } while (Process32Next(snap, &pe));
          }
          CloseHandle(snap);
      }

      WaitForProcessesDead(targets, 2500);
      Sleep(500);
  }

  // ============================================================
  //  FindHdAdbPath
  // ============================================================
  static std::string FindHdAdbPath()
  {
      const char* candidates[] = {
          "C:\\Program Files\\BlueStacks_nxt\\HD-Adb.exe",
          "C:\\Program Files (x86)\\BlueStacks_nxt\\HD-Adb.exe",
          "C:\\Program Files\\BlueStacks_msi5\\HD-Adb.exe",
          "C:\\Program Files (x86)\\BlueStacks_msi5\\HD-Adb.exe",
          "C:\\Program Files\\BlueStack Systems\\BlueStacks App Player\\HD-Adb.exe",
          "C:\\Program Files (x86)\\BlueStack Systems\\BlueStacks App Player\\HD-Adb.exe",
          "C:\\Program Files\\BlueStacks 5\\HD-Adb.exe",
          "C:\\Program Files (x86)\\BlueStacks 5\\HD-Adb.exe",
          nullptr
      };
      for (int i = 0; candidates[i]; ++i)
          if (GetFileAttributesA(candidates[i]) != INVALID_FILE_ATTRIBUTES)
              return candidates[i];

      const char* regKeys[] = {
          "SOFTWARE\\BlueStacks_nxt",
          "SOFTWARE\\WOW6432Node\\BlueStacks_nxt",
          "SOFTWARE\\BlueStacks_msi5",
          "SOFTWARE\\WOW6432Node\\BlueStacks_msi5",
          "SOFTWARE\\BlueStack Systems\\BlueStacks App Player",
          "SOFTWARE\\WOW6432Node\\BlueStack Systems\\BlueStacks App Player",
          nullptr
      };
      HKEY hKey = nullptr;
      for (int i = 0; regKeys[i]; ++i)
      {
          if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKeys[i], 0, KEY_READ, &hKey) == ERROR_SUCCESS)
          {
              char val[MAX_PATH] = {};
              DWORD sz = MAX_PATH;
              if (RegQueryValueExA(hKey, "InstallDir", nullptr, nullptr,
                                   (LPBYTE)val, &sz) == ERROR_SUCCESS)
              {
                  RegCloseKey(hKey); hKey = nullptr;
                  std::string p = std::string(val) + "\\HD-Adb.exe";
                  if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES)
                      return p;
              }
              if (hKey) { RegCloseKey(hKey); hKey = nullptr; }
          }
      }
      return "";
  }

  // ============================================================
  //  RunProcess — spawn and capture stdout+stderr
  // ============================================================
  static std::string RunProcess(const char* exe, const char* args, DWORD timeoutMs = 7000)
  {
      if (!args) return "";

      char fullCmd[2048];
      int needed;
      if (exe && exe[0])
          needed = snprintf(fullCmd, sizeof(fullCmd), "\"%s\" %s", exe, args);
      else
          needed = snprintf(fullCmd, sizeof(fullCmd), "%s", args);

      if (needed <= 0 || needed >= (int)sizeof(fullCmd)) return "";

      SECURITY_ATTRIBUTES sa{};
      sa.nLength = sizeof(sa);
      sa.bInheritHandle = TRUE;

      HANDLE hRead = nullptr, hWrite = nullptr;
      if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";
      SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

      STARTUPINFOA si{};
      si.cb        = sizeof(si);
      si.dwFlags   = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      si.hStdOutput = hWrite;
      si.hStdError  = hWrite;
      si.wShowWindow = SW_HIDE;

      PROCESS_INFORMATION pi{};
      if (!CreateProcessA(nullptr, fullCmd, nullptr, nullptr,
                          TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
      {
          CloseHandle(hRead);
          CloseHandle(hWrite);
          return "";
      }
      CloseHandle(hWrite);

      if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_TIMEOUT)
          TerminateProcess(pi.hProcess, 1);

      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);

      std::string result;
      char buf[512];
      DWORD bytesRead = 0;
      while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
      {
          buf[bytesRead] = '\0';
          result += buf;
      }
      CloseHandle(hRead);
      return result;
  }

  // ============================================================
  //  AdbShell
  // ============================================================
  std::string AdbShell(const char* cmd)
  {
      if (!cmd) return "";
      char args[1024];
      snprintf(args, sizeof(args), "shell %s", cmd);
      return RunProcess("adb", args, 5000);
  }

  // ============================================================
  //  VerifyAdbServer
  //  Runs "adb devices" and returns true if at least one device
  //  (emulator) is listed — meaning the server is up and the
  //  emulator is reachable.  Falls back to true if a device line
  //  contains "emulator" or "127.0.0.1".
  // ============================================================
  static bool VerifyAdbServer(const std::string& adbExe)
  {
      const std::string& exe = adbExe.empty() ? std::string("adb") : adbExe;
      std::string out = RunProcess(exe.c_str(), "devices", 6000);
      if (out.empty()) return false;

      std::istringstream ss(out);
      std::string line;
      while (std::getline(ss, line))
      {
          // Skip the header line
          if (line.find("List of devices") != std::string::npos) continue;

          std::string lo = line;
          std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);

          // A real device/emulator line ends with "device", "offline", etc.
          if ((lo.find("emulator") != std::string::npos ||
               lo.find("127.0.0.1") != std::string::npos ||
               lo.find("localhost") != std::string::npos) &&
              lo.find("\t") != std::string::npos)
          {
              return true;
          }
      }
      return false;
  }

  // ============================================================
  //  TryExplicitAdbConnect
  //  Tries "adb connect 127.0.0.1:<port>" on every port that
  //  BlueStacks / MSI App Player is known to use.  Returns true
  //  if any connect succeeds (output contains "connected").
  //  Called when "adb devices" sees no device after start-server.
  // ============================================================
  static bool TryExplicitAdbConnect(const std::string& adbExe)
  {
      // BlueStacks 5 / MSI default range + common instance offsets
      static const int kPorts[] = {
          5555, 5556, 5565, 5575, 5585, 5595,
          5554, 5558, 5562, 5572, 5582, 5592,
          0
      };

      const std::string& exe = adbExe.empty() ? std::string("adb") : adbExe;
      bool anyConnected = false;

      for (int i = 0; kPorts[i]; ++i)
      {
          char args[64];
          snprintf(args, sizeof(args), "connect 127.0.0.1:%d", kPorts[i]);
          std::string out = RunProcess(exe.c_str(), args, 4000);

          std::string lo = out;
          std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);

          if (lo.find("connected") != std::string::npos &&
              lo.find("unable")    == std::string::npos &&
              lo.find("failed")    == std::string::npos)
          {
              anyConnected = true;
              // Don't break — connect to all instances so we get every emulator
          }
      }
      return anyConnected;
  }

  // ============================================================
  //  TryStartAdbServer
  //  Starts the ADB server, verifies it's actually serving a
  //  device, and optionally falls back to an explicit connect.
  //  Returns true if a device is reachable after the attempts.
  // ============================================================
  static bool TryStartAdbServer(const std::string& adbExe, int maxAttempts = 3)
  {
      for (int attempt = 0; attempt < maxAttempts; ++attempt)
      {
          if (attempt > 0)
          {
              // Kill leftover server before retrying
              RunProcess(adbExe.empty() ? "adb" : adbExe.c_str(), "kill-server", 4000);
              Sleep(600);
          }

          RunProcess(adbExe.empty() ? "adb" : adbExe.c_str(), "start-server", 6000);
          Sleep(400 + attempt * 300);   // give the server a bit more time each retry

          if (VerifyAdbServer(adbExe))
              return true;

          // Server started but no device listed — try an explicit TCP connect
          if (TryExplicitAdbConnect(adbExe))
          {
              Sleep(300);
              if (VerifyAdbServer(adbExe))
                  return true;
          }
      }
      return false;
  }

  // ============================================================
  //  ParseBaseAddress helpers
  // ============================================================
  static std::string ExtractFirstHex(const std::string& line)
  {
      for (size_t pos = 4; pos < line.size(); ++pos)
      {
          if (line[pos] != '-') continue;
          for (int len : {16, 8, 7, 6})
          {
              if ((int)pos < len) continue;
              size_t start = pos - len;
              bool ok = true;
              for (size_t k = start; k < pos; ++k)
              {
                  char c = line[k];
                  if (!((c >= '0' && c <= '9') ||
                        (c >= 'a' && c <= 'f') ||
                        (c >= 'A' && c <= 'F')))
                  { ok = false; break; }
              }
              if (ok) return line.substr(start, len);
          }
      }
      return "";
  }

  uintptr_t ParseBaseAddress(const std::string& line)
  {
      if (line.empty()) return 0;
      std::string hex = ExtractFirstHex(line);
      if (!hex.empty())
          return (uintptr_t)strtoull(hex.c_str(), nullptr, 16);
      unsigned long long tmp = 0;
      sscanf_s(line.c_str(), "%llx", &tmp);
      return (uintptr_t)tmp;
  }

  uintptr_t ParseBaseAddress2(const std::string& line)
  {
      return ParseBaseAddress(line);
  }

  // ============================================================
  //  FindIl2CppLine helper
  // ============================================================
  static const char* s_grepPatterns[] = {
      "libil2cpp.so", "il2cpp.so", "libil2cpp", "il2cpp", nullptr
  };

  static std::string FindIl2CppLine(const std::string& out)
  {
      if (out.empty()) return "";
      std::istringstream ss(out);
      std::string line;
      while (std::getline(ss, line))
      {
          std::string lo = line;
          std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
          if (lo.find("il2cpp") != std::string::npos &&
              lo.find('-')      != std::string::npos)
              return line;
      }
      return "";
  }

  // ============================================================
  //  Interactive ADB shell
  // ============================================================
  struct InteractiveShell {
      HANDLE hProcess  = nullptr;
      HANDLE hThread   = nullptr;
      HANDLE hStdinWr  = nullptr;
      HANDLE hStdoutRd = nullptr;
      bool   alive     = false;

      void Cleanup()
      {
          if (hStdinWr)  { CloseHandle(hStdinWr);  hStdinWr  = nullptr; }
          if (hStdoutRd) { CloseHandle(hStdoutRd); hStdoutRd = nullptr; }
          if (hProcess)
          {
              if (WaitForSingleObject(hProcess, 500) != WAIT_OBJECT_0)
                  TerminateProcess(hProcess, 0);
              CloseHandle(hProcess);
              hProcess = nullptr;
          }
          if (hThread) { CloseHandle(hThread); hThread = nullptr; }
          alive = false;
      }
  };

  static const char* s_suPaths[] = {
      "/boot/android/android/system/xbin/bstk/su",
      "/boot/android/android/system/xbin/msi/su",
      "/system/xbin/su",
      "/system/bin/su",
      "/sbin/su",
      nullptr
  };

  static bool StartInteractiveShell(InteractiveShell& sh,
                                     const std::string& adbExe,
                                     const char* suPath)
  {
      if (!suPath) return false;

      SECURITY_ATTRIBUTES sa{};
      sa.nLength        = sizeof(sa);
      sa.bInheritHandle = TRUE;

      HANDLE hStdinRd  = nullptr, hStdinWr  = nullptr;
      HANDLE hStdoutRd = nullptr, hStdoutWr = nullptr;

      if (!CreatePipe(&hStdinRd,  &hStdinWr,  &sa, 0)) return false;
      if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0))
      {
          CloseHandle(hStdinRd); CloseHandle(hStdinWr);
          return false;
      }

      SetHandleInformation(hStdinWr,  HANDLE_FLAG_INHERIT, 0);
      SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);

      char cmdLine[2048];
      snprintf(cmdLine, sizeof(cmdLine),
               "\"%s\" shell \"getprop ro.secure ; %s\"",
               adbExe.c_str(), suPath);

      STARTUPINFOA si{};
      si.cb        = sizeof(si);
      si.dwFlags   = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
      si.hStdInput  = hStdinRd;
      si.hStdOutput = hStdoutWr;
      si.hStdError  = hStdoutWr;
      si.wShowWindow = SW_HIDE;

      PROCESS_INFORMATION pi{};
      BOOL ok = CreateProcessA(nullptr, cmdLine, nullptr, nullptr,
                               TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

      CloseHandle(hStdinRd);
      CloseHandle(hStdoutWr);

      if (!ok)
      {
          CloseHandle(hStdinWr);
          CloseHandle(hStdoutRd);
          return false;
      }

      sh.hProcess  = pi.hProcess;
      sh.hThread   = pi.hThread;
      sh.hStdinWr  = hStdinWr;
      sh.hStdoutRd = hStdoutRd;
      sh.alive     = true;
      return true;
  }

  static void ShellSend(InteractiveShell& sh, const char* cmd)
  {
      if (!sh.alive || !sh.hStdinWr || !cmd) return;
      std::string line = std::string(cmd) + "\n";
      DWORD written = 0;
      WriteFile(sh.hStdinWr, line.c_str(), (DWORD)line.size(), &written, nullptr);
  }

  static std::string ShellRead(InteractiveShell& sh, DWORD timeoutMs = 3000)
  {
      if (!sh.alive || !sh.hStdoutRd) return "";

      std::string result;
      char buf[4096];
      ULONGLONG startTick = GetTickCount64();

      while ((GetTickCount64() - startTick) < timeoutMs)
      {
          DWORD avail = 0;
          if (!PeekNamedPipe(sh.hStdoutRd, nullptr, 0, nullptr, &avail, nullptr))
              break;

          if (avail > 0)
          {
              DWORD toRead    = (avail < sizeof(buf) - 1) ? avail : (sizeof(buf) - 1);
              DWORD bytesRead = 0;
              if (ReadFile(sh.hStdoutRd, buf, toRead, &bytesRead, nullptr) && bytesRead > 0)
              {
                  buf[bytesRead] = '\0';
                  result += buf;
              }
              startTick = GetTickCount64();
          }
          else
          {
              Sleep(10);
          }
      }
      return result;
  }

  static bool ShellWaitForOutput(InteractiveShell& sh, DWORD waitMs = 5000)
  {
      if (!sh.alive || !sh.hStdoutRd) return false;
      ULONGLONG start = GetTickCount64();
      while ((GetTickCount64() - start) < waitMs)
      {
          DWORD avail = 0;
          if (PeekNamedPipe(sh.hStdoutRd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0)
              return true;
          Sleep(5);
      }
      return false;
  }

  static std::string ExtractPID(const std::string& psOutput, const std::string& processName)
  {
      if (psOutput.empty() || processName.empty()) return "";

      std::istringstream ss(psOutput);
      std::string line;
      while (std::getline(ss, line))
      {
          if (line.find(processName) == std::string::npos) continue;

          std::istringstream ls(line);
          std::string tok;
          std::vector<std::string> parts;
          while (ls >> tok) parts.push_back(tok);

          if (parts.size() > 1) return parts[1];
      }
      return "";
  }

  // ============================================================
  //  InteractiveShellFindModule
  // ============================================================
  static uintptr_t InteractiveShellFindModule(const char* adbExePath)
  {
      const std::string adbExe = (adbExePath && adbExePath[0]) ? adbExePath : "adb";

      for (int si = 0; s_suPaths[si]; ++si)
      {
          const char* suPath = s_suPaths[si];
          InteractiveShell sh{};

          if (!StartInteractiveShell(sh, adbExe, suPath)) continue;

          if (!ShellWaitForOutput(sh, 5000)) { sh.Cleanup(); continue; }

          ShellRead(sh, 1000);   // drain banner

          ShellSend(sh, "ps");
          Sleep(500);
          std::string psOut = ShellRead(sh, 3000);
          std::string pid   = ExtractPID(psOut, Config::GamePackage);

          if (pid.empty())
          {
              ShellSend(sh, "ps -A");
              Sleep(500);
              psOut = ShellRead(sh, 3000);
              pid   = ExtractPID(psOut, Config::GamePackage);
          }
          if (pid.empty())
          {
              std::string pidofCmd = "pidof " + Config::GamePackage;
              ShellSend(sh, pidofCmd.c_str());
              Sleep(300);
              std::string pidofOut = ShellRead(sh, 2000);
              std::istringstream pss(pidofOut);
              std::string tok;
              while (pss >> tok)
              {
                  bool isNum = !tok.empty();
                  for (char c : tok) { if (c < '0' || c > '9') { isNum = false; break; } }
                  if (isNum && tok.size() >= 1 && tok.size() <= 6) { pid = tok; break; }
              }
          }

          if (pid.empty()) { sh.Cleanup(); continue; }

          uintptr_t baseAddr = 0;
          for (const char* pat : s_grepPatterns)
          {
              if (!pat) break;
              char mapsCmd[256];
              snprintf(mapsCmd, sizeof(mapsCmd),
                       "cat /proc/%s/maps | grep %s | head -1",
                       pid.c_str(), pat);
              ShellSend(sh, mapsCmd);
              Sleep(300);
              std::string mapsOut = ShellRead(sh, 2000);
              std::string line    = FindIl2CppLine(mapsOut);
              if (!line.empty())
              {
                  baseAddr = ParseBaseAddress(line);
                  if (baseAddr != 0 && baseAddr <= 0xFFFFFFFFull) break;
                  baseAddr = 0;
              }
          }

          sh.Cleanup();
          if (baseAddr != 0) return baseAddr;
      }
      return 0;
  }

  // ============================================================
  //  ShellGetAddressRobust (legacy fallback)
  // ============================================================
  static constexpr int MAX_OUTER_RETRIES = 5;

  static std::string TryShell(const std::string& exe,
                               const std::string& shellArgs,
                               const std::string& grepPattern,
                               DWORD timeout)
  {
      std::string cmd = "shell " + shellArgs +
          " \"cat /proc/$(pidof " + Config::GamePackage +
          ")/maps | grep " + grepPattern + " | head -3\"";
      std::string out = RunProcess(exe.empty() ? "adb" : exe.c_str(),
                                   cmd.c_str(), timeout);
      return FindIl2CppLine(out);
  }

  static std::string ShellGetAddressRobust()
  {
      std::string hdAdb = FindHdAdbPath();

      struct Attempt { std::string exe; std::string prefix; DWORD timeout; };
      std::vector<Attempt> attempts;

      if (!hdAdb.empty())
      {
          attempts.push_back({ hdAdb, "su -c",   8000 });
          attempts.push_back({ hdAdb, "su 0 -c", 8000 });
          attempts.push_back({ hdAdb, "",         5000 });
      }
      attempts.push_back({ "adb", "su -c",   8000 });
      attempts.push_back({ "adb", "su 0 -c", 8000 });
      attempts.push_back({ "adb", "",         5000 });

      for (const char* pat : s_grepPatterns)
      {
          if (!pat) break;
          for (const auto& a : attempts)
          {
              std::string line = TryShell(a.exe, a.prefix, pat, a.timeout);
              if (!line.empty()) return line;
          }
      }
      return "";
  }

  // ============================================================
  //  FindRenderWindow
  // ============================================================
  static HWND g_renderWindow = nullptr;

  static BOOL CALLBACK EnumChildRenderProc(HWND hWnd, LPARAM)
  {
      char windowName[256]{}, className[256]{};
      GetWindowTextA(hWnd,  windowName, sizeof(windowName));
      GetClassNameA(hWnd,   className,  sizeof(className));
      std::string name(windowName), cls(className);
      if (name == "_ctl.Window" && cls.find("BlueStacksApp") != std::string::npos)
          { g_renderWindow = hWnd; return FALSE; }
      if (name == "HD-Player" && cls.find("Qt") != std::string::npos)
          { g_renderWindow = hWnd; return FALSE; }
      return TRUE;
  }

  static BOOL CALLBACK EnumTopRenderProc(HWND hWnd, LPARAM lParam)
  {
      DWORD targetPid = *(DWORD*)lParam, wndPid = 0;
      GetWindowThreadProcessId(hWnd, &wndPid);
      if (wndPid == targetPid) EnumChildWindows(hWnd, EnumChildRenderProc, 0);
      return TRUE;
  }

  HWND FindRenderWindow()
  {
      g_renderWindow = nullptr;
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
      if (snap != INVALID_HANDLE_VALUE)
      {
          PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
          if (Process32FirstW(snap, &pe))
          {
              do {
                  if (_wcsicmp(pe.szExeFile, L"HD-Player.exe") == 0)
                      EnumWindows(EnumTopRenderProc, (LPARAM)&pe.th32ProcessID);
              } while (Process32NextW(snap, &pe) && !g_renderWindow);
          }
          CloseHandle(snap);
      }
      if (g_renderWindow) return g_renderWindow;

      const char* names[] = {
          "BlueStacks App Player", "MSI App Player",
          "BlueStacks", "App Player", nullptr
      };
      for (int i = 0; names[i]; ++i)
      {
          HWND hw = FindWindowA(nullptr, names[i]);
          if (hw) return hw;
      }
      return nullptr;
  }

  // ============================================================
  //  SEH_InteractiveShellFindModule
  // ============================================================
  static uintptr_t SEH_InteractiveShellFindModule(const char* exe)
  {
      __try   { return InteractiveShellFindModule(exe); }
      __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
  }

  // ============================================================
  //  ADBInitThread
  //
  //  Cross-instance safety:
  //   A named kernel mutex (ADB_INIT_MUTEX) serialises ADB init
  //   across every HD-Player.exe process.  When two windows are
  //   open, the second instance waits up to 90 s for the first
  //   to finish, then checks whether AdbReady was already set by
  //   the winner — if so, it re-uses the result and skips its
  //   own kill+restart cycle, preventing the ADB server from
  //   being torn down while the other DLL is mid-query.
  // ============================================================
  static DWORD WINAPI ADBInitThread(LPVOID)
  {
      Config::AdbBusy.store(true);

      // ── Acquire cross-process serialisation mutex ─────────────────────────
      // CREATE_MUTEX_INITIALSTATE=FALSE, non-recursive.
      HANDLE hMtx = CreateMutexW(nullptr, FALSE, ADB_INIT_MUTEX);
      bool ownsMtx = false;
      if (hMtx)
      {
          // Wait up to 90 s — another instance's full init pass takes ~60 s.
          DWORD wr = WaitForSingleObject(hMtx, 90000);
          ownsMtx = (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED);
      }

      // ── Fast path: ADB was initialised by a concurrent instance ──────────
      // Check this AFTER acquiring (or failing to acquire) the mutex so we
      // see the write the winner made before it released.
      if (g_adbInitialized.load() && Config::AdbReady.load())
      {
          // Another DLL instance already set everything up.  Reuse its result.
          if (hMtx && ownsMtx) ReleaseMutex(hMtx);
          if (hMtx) CloseHandle(hMtx);
          Config::AdbBusy.store(false);
          g_adbThreadRunning.store(false);
          return 0;
      }

      std::string hdAdb = FindHdAdbPath();
      const std::string adbExe = hdAdb.empty() ? "adb" : hdAdb;

      // ── Kill stale ADB and start a fresh server ───────────────────────────
      KillAdbZ();

      // Start server with retry + device verify + explicit-connect fallback.
      // If after 3 attempts no device is seen, we still proceed — the interactive
      // shell fallback may find the game via a local loopback socket that "adb
      // devices" misses due to transport-id confusion.
      bool serverReady = TryStartAdbServer(adbExe, 3);
      if (!serverReady)
      {
          // One final try: explicit kill-server, fresh start, then verify
          RunProcess(adbExe.c_str(), "kill-server", 4000);
          Sleep(800);
          RunProcess(adbExe.c_str(), "start-server", 6000);
          Sleep(600);
          TryExplicitAdbConnect(adbExe);
          Sleep(300);
          serverReady = VerifyAdbServer(adbExe);
      }

      uintptr_t il2cpp = 0;

      // Method 1: interactive shell with self-root su.
      for (int attempt = 0; attempt < 3 && il2cpp == 0; ++attempt)
      {
          il2cpp = SEH_InteractiveShellFindModule(adbExe.c_str());
          if (il2cpp == 0 && attempt < 2)
              std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }

      // Method 2: legacy one-shot fallback.
      if (il2cpp == 0)
      {
          std::string line;
          for (int attempt = 0; attempt < MAX_OUTER_RETRIES && line.empty(); ++attempt)
          {
              line = ShellGetAddressRobust();
              if (line.empty())
              {
                  // If the ADB server seems dead, attempt a reconnect before
                  // the next retry — the emulator may have reset its transport.
                  if (attempt > 0 && !VerifyAdbServer(adbExe))
                  {
                      TryExplicitAdbConnect(adbExe);
                      Sleep(300);
                  }
                  DWORD ms = 500u * (1u << std::min(attempt, 4));
                  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
              }
          }
          if (!line.empty())
              il2cpp = ParseBaseAddress(line);
      }

      if (il2cpp != 0 && il2cpp <= 0xFFFFFFFFull)
      {
          Config::Il2CppBase.store(il2cpp);
          Config::AdbReady.store(true);
          g_adbInitialized.store(true);
      }

      if (hMtx && ownsMtx) ReleaseMutex(hMtx);
      if (hMtx) CloseHandle(hMtx);

      Config::AdbBusy.store(false);
      g_adbThreadRunning.store(false);
      return 0;
  }

  // ============================================================
  //  StartAdbInitThread
  // ============================================================
  void StartAdbInitThread()
  {
      bool expected = false;
      if (!g_adbThreadRunning.compare_exchange_strong(expected, true))
          return;

      Config::AdbBusy.store(true);
      HANDLE h = CreateThread(nullptr, 0, ADBInitThread, nullptr, 0, nullptr);
      if (h)
      {
          SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL);
          CloseHandle(h);
      }
      else
      {
          Config::AdbBusy.store(false);
          g_adbThreadRunning.store(false);
      }
  }
  