// Launches the release executable with no arguments and verifies it stays alive.
// The original bug made the window vanish within milliseconds (exit code 5), so
// this test fails on the old binary and passes once the message loop is gated.
#include <windows.h>

#include <cstdio>
#include <string>

#ifndef BANSHOU_SOURCE_DIR
#error "BANSHOU_SOURCE_DIR must be defined"
#endif

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::printf("FAIL: %s\n", message);
    ++failures;
  }
}

struct WindowSearch {
  DWORD pid = 0;
  HWND main = nullptr;
  HWND dialog = nullptr;
};

BOOL CALLBACK findWindow(HWND window, LPARAM parameter) {
  auto *search = reinterpret_cast<WindowSearch *>(parameter);
  DWORD owner = 0;
  GetWindowThreadProcessId(window, &owner);
  if (owner != search->pid)
    return TRUE;
  wchar_t className[64] = {};
  GetClassNameW(window, className, 63);
  if (wcscmp(className, L"BanshouNativeWindow") == 0 && IsWindowVisible(window))
    search->main = window;
  if (wcscmp(className, L"#32770") == 0)
    search->dialog = window;
  return TRUE;
}

}  // namespace

int main() {
  const std::wstring root = L"" BANSHOU_SOURCE_DIR;
  const std::wstring exe = root + L"\\dist\\扳手.exe";
  if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
    std::printf("dist executable missing, skipping startup_alive\n");
    return 0;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  std::wstring command = L"\"" + exe + L"\"";
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &startup, &process)) {
    std::printf("FAIL: cannot launch %ls\n", exe.c_str());
    return 1;
  }

  // 3 seconds: the buggy build exits in a few milliseconds here.
  const DWORD waited = WaitForSingleObject(process.hProcess, 3000);
  DWORD exitCode = 0;
  GetExitCodeProcess(process.hProcess, &exitCode);
  check(waited == WAIT_TIMEOUT, "process exited within 3 seconds");
  check(exitCode == STILL_ACTIVE, "process is no longer active");

  WindowSearch search;
  search.pid = process.dwProcessId;
  EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
  check(search.main != nullptr, "main window was not created");
  check(search.dialog == nullptr, "an error dialog appeared");

  if (search.main) {
    PostMessageW(search.main, WM_CLOSE, 0, 0);
    const DWORD closed = WaitForSingleObject(process.hProcess, 5000);
    check(closed == WAIT_OBJECT_0, "window did not close on WM_CLOSE");
    exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    check(exitCode == 0, "WM_CLOSE must exit with code 0");
  } else {
    TerminateProcess(process.hProcess, 0);
  }

  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);

  if (failures) {
    std::printf("%d checks failed\n", failures);
    return 1;
  }
  std::printf("startup alive contracts passed\n");
  return 0;
}
