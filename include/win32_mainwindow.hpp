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

// Native Win32 main window.  Control geometry, text and font sizes come from
// src/win32_control_table.inc, a frozen snapshot of the original design.
class Win32Business;
struct WinControlDesc;

class Win32MainWindow final {
public:
  explicit Win32MainWindow(HINSTANCE instance, bool diagnostic = false);
  ~Win32MainWindow();

  bool create();
  HWND handle() const { return hwnd_; }
  HWND frame() const { return frame_; }
  HWND control(const wchar_t *name) const;
  HWND pagePanel(int page) const;
  HWND toolButton(int index) const;
  int currentPage() const { return currentPage_; }
  int toolbarHeight() const { return toolbarHeight_; }
  void showPage(int page);
  void applyInitialState();

  const std::wstring &startupError() const { return startupError_; }

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
  int addRow(const wchar_t *name, const std::vector<std::wstring> &cells,
             uint64_t data);
  std::wstring tableCellText(const wchar_t *name, int row, int column) const;
  int tableColumnCount(const wchar_t *name) const;
  bool isListView(const wchar_t *name) const;
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
  HBRUSH onCtlColor(UINT message, HDC dc, HWND child);
  void paintTool(DRAWITEMSTRUCT *item);

  // JSON produced by --diagnostic-output.
  std::string diagnosticSnapshot() const;

private:
  int idOf(const wchar_t *name) const;
  bool createImpl();
  void wireBusiness();
  void createFrame();
  void createControls();
  void createTools();
  void beginWindowSelection();
  void hoverWindowSelection();
  void finishWindowSelection(bool attach);
  static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM wparam,
                               LPARAM lparam);
  static LRESULT CALLBACK containerProc(HWND hwnd, UINT message, WPARAM wparam,
                                        LPARAM lparam, UINT_PTR id,
                                        DWORD_PTR ref);

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND frame_ = nullptr;
  std::vector<HWND> controls_;
  std::unordered_map<std::wstring, HWND> byName_;
  std::unordered_map<int, const struct WinControlDesc *> byId_;
  HWND toolButtons_[8] = {};
  HFONT defaultFont_ = nullptr;
  std::vector<HFONT> fonts_;
  std::unordered_map<int, HFONT> fontBySize_;
  HWND pages_[9] = {};
  int currentPage_ = 0;
  int toolbarHeight_ = 30;
  bool diagnostic_ = false;
  std::shared_ptr<MemorySession> memory_;
  EngineProfile profile_;
  uint64_t sessionGeneration_ = 0;
  bool suppressNotifications_ = false;
  bool selecting_ = false;
  HWND hoverWindow_ = nullptr;
  HWND editCell_ = nullptr;
  std::wstring statusText_;
  std::wstring startupError_;
  DiyCsv diy_;
  std::wstring diyPath_;
  size_t diySection_ = 0;
  size_t diyOffset_ = 0;
  std::vector<DiyExpandedRow> diyVisible_;
  HBRUSH faceBrush_ = nullptr;
  std::unordered_map<COLORREF, HBRUSH> brushes_;

  // business_ must outlive the handlers that capture it.
  std::unique_ptr<Win32Business> business_;
  std::function<void(int, HWND, WORD)> commandHandler_;
  std::function<void(NMHDR *)> notifyHandler_;
  std::function<void(UINT_PTR)> timerHandler_;
  std::function<void(HWND, POINT)> contextMenuHandler_;
  friend class Win32Business;
struct WinControlDesc;
};

int runWindow(int argc, wchar_t **argv);

}  // namespace banshou
