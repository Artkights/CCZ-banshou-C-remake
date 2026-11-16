#include <windows.h>
#include <cstdio>
#include <shellapi.h>
#include <string>

extern "C" __declspec(noinline) void spin_tick() {
  Sleep(20);
}

static void writeAddressFile(const wchar_t *path, uint32_t reference) {
  if (!path || !*path)
    return;
  wchar_t text[32]{};
  swprintf_s(text, L"0x%08X\n", reference);
  HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return;
  DWORD written = 0;
  WriteFile(file, text, DWORD(wcslen(text) * sizeof(wchar_t)), &written,
            nullptr);
  CloseHandle(file);
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int APIENTRY wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool spin = false;
  std::wstring addressPath;
  for (int i = 1; i < argc; ++i) {
    std::wstring argument = argv[i];
    if (argument == L"--breakpoint-spin")
      spin = true;
    else if (argument == L"--address-file" && i + 1 < argc)
      addressPath = argv[++i];
  }
  LocalFree(argv);
  WNDCLASSW c{};
  c.hInstance = hi;
  c.lpfnWndProc = proc;
  c.lpszClassName = L"BanshouMockTarget";
  RegisterClassW(&c);
  auto h = CreateWindowW(c.lpszClassName, L"Banshou Mock Target",
                         WS_OVERLAPPEDWINDOW, 20, 20, 300, 200, nullptr,
                         nullptr, hi, nullptr);
  ShowWindow(h, SW_SHOW);
  if (spin) {
    const auto module = uint32_t(
        reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)));
    const auto actual =
        uint32_t(reinterpret_cast<uintptr_t>(&spin_tick));
    writeAddressFile(addressPath.c_str(), actual - module + 0x400000u);
  }
  MSG message;
  while (true) {
    if (spin)
      spin_tick();
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT)
        return 0;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(spin ? 0 : 5);
  }
}
