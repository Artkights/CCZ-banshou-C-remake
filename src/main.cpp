#include "evidence.hpp"
#include "win32_mainwindow.hpp"
#include <shellapi.h>
#include <string>
int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  (void)instance;
  banshou::installDiagnostics();
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc > 1) {
    const std::wstring command = argv[1];
    if (command == L"--evidence" || command == L"--evidence-static" ||
        command == L"--evidence-oracle" || command == L"--evidence-runtime" ||
        command == L"--evidence-inspect" || command == L"--evidence-verify" ||
        command == L"--evidence-manifest") {
      auto rc = banshou::evidenceCli(argc, argv);
      LocalFree(argv);
      return rc;
    }
  }
  auto rc = banshou::runWindow(argc, argv);
  LocalFree(argv);
  return rc;
}
