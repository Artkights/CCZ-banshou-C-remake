#pragma once

#include <windows.h>
#include "diy.hpp"
#include "memory.hpp"
#include "profile.hpp"
#include "session.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace banshou {

// Native Win32 replacement for QtMainWindow.  The control geometry, text and
// font sizes come from src/win32_control_table.inc, generated from the former
// ui/mainwindow.ui so the layout is reproduced exactly.
class Win32Business;

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
  void toggleControl(int index);
  std::shared_ptr<MemorySession> session() const { return memory_; }
  EngineProfile profile() const { return profile_; }

  // Low-level control helpers used by the business layer.
  std::wstring text(const wchar_t *name) const;
  void setText(const wchar_t *name, const std::wstring &value);
  void setText(const wchar_t *name, uint64_t value);
  uint64_t number(const wchar_t *name, uint64_t maximum,
                  const char *label) const;
  int32_t signedNumber(const wchar_t *name, const char *label) const;
  bool checked(const wchar_t *name) const;
  void setChecked(const wchar_t *name, bool value);
  int comboIndex(const wchar_t *name) const;
  void setComboIndex(const wchar_t *name, int index);
  uint64_t comboData(const wchar_t *name, uint64_t fallback = 0) const;
  bool selectComboData(const wchar_t *name, uint64_t value);
  int addCombo(const wchar_t *name, const std::wstring &text, uint64_t data);
  void clearControl(const wchar_t *name);
  int listCount(const wchar_t *name) const;
  int listSelection(const wchar_t *name) const;
  uint64_t listData(const wchar_t *name, int row) const;
  void setListData(const wchar_t *name, int row, uint64_t data);
  int addList(const wchar_t *name, const std::wstring &text, uint64_t data);
  void setEnabled(const wchar_t *name, bool enabled);
  void setVisible(const wchar_t *name, bool visible);
  void setRect(const wchar_t *name, int x, int y, int width, int height);

  DiyCsv &diy() { return diy_; }
  std::wstring &diyPath() { return diyPath_; }
  size_t &diySection() { return diySection_; }
  size_t &diyOffset() { return diyOffset_; }
  std::vector<DiyExpandedRow> &diyVisible() { return diyVisible_; }

  void setStatus(const std::wstring &text);
  void onCommand(int id, HWND control, WORD notification);
  void onNotify(NMHDR *header);
  void onTimer(UINT_PTR timer);
  void onSessionState(uint64_t generation);

  // JSON produced by --diagnostic-output.
  std::string diagnosticSnapshot() const;

private:
  int idOf(const wchar_t *name) const;
  void wireBusiness();
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
  DiyCsv diy_;
  std::wstring diyPath_;
  size_t diySection_ = 0;
  size_t diyOffset_ = 0;
  std::vector<DiyExpandedRow> diyVisible_;
  std::function<void(int, HWND, WORD)> commandHandler_;
  std::function<void(NMHDR *)> notifyHandler_;
  std::function<void(UINT_PTR)> timerHandler_;
  std::function<void(HWND, POINT)> contextMenuHandler_;
  std::unique_ptr<Win32Business> business_;
  friend class Win32Business;
};

int runWindow(int argc, wchar_t **argv);

}  // namespace banshou
