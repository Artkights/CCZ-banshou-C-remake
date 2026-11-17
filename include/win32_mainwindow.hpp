#pragma once

#include <windows.h>
#include "memory.hpp"
#include "profile.hpp"
#include "session.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace banshou {

// Native Win32 replacement for QtMainWindow.  The control geometry, text and
// font sizes come from src/win32_control_table.inc, generated from the former
// ui/mainwindow.ui so the layout is reproduced exactly.
class Win32MainWindow final {
public:
  explicit Win32MainWindow(HINSTANCE instance, bool diagnostic = false);
  ~Win32MainWindow();

  bool create();
  HWND handle() const { return hwnd_; }
  HWND control(const wchar_t *name) const;
  int currentPage() const { return currentPage_; }
  void showPage(int page);

  bool attachPid(uint32_t pid, bool readOnly = false);
  void refreshWarehouse();
  void toggleControl(int index);
  void setStatus(const std::wstring &text);
  void onCommand(int id, HWND control, WORD notification);
  void onNotify(NMHDR *header);
  void onTimer(UINT_PTR timer);
  void onSessionState(uint64_t generation);

  // JSON produced by --diagnostic-output.
  std::string diagnosticSnapshot() const;

private:
  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam);

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  std::vector<HWND> controls_;
  std::unordered_map<std::wstring, HWND> byName_;
  HWND toolButtons_[8] = {};
  std::vector<HFONT> fonts_;
  std::unordered_map<int, HFONT> fontBySize_;
  HWND pages_[8] = {};
  int currentPage_ = 0;
  bool diagnostic_ = false;
  std::shared_ptr<MemorySession> memory_;
  EngineProfile profile_;
  uint64_t sessionGeneration_ = 0;
  bool suppressNotifications_ = false;
  std::wstring statusText_;
};

int runWindow(int argc, wchar_t **argv);

}  // namespace banshou
