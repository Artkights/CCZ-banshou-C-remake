#include "win32_mainwindow.hpp"
#include "win32_business.hpp"
#include <shlobj.h>
#include <game.hpp>
#include <image.hpp>
#include <windowsx.h>
#include "platform.hpp"
#include "game.hpp"
#include "image.hpp"

#include "win32_control_table.inc"

#include <commctrl.h>
#include <algorithm>
#include <cstdlib>
#include <json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace banshou {
namespace {
const char *const kNativePatchKeys[] = {
    "control_friendly", "control_all_factions", "control_no_ai",
    "control_move_after_wait", "control_move_255", "control_pass_through",
    "control_auto_return"};

constexpr UINT kSessionStateMessage = WM_APP + 1;
constexpr UINT_PTR kSmokeTimer = 1;
constexpr UINT_PTR kDiagnosticTimer = 2;

struct StartupOptions {
  std::optional<uint32_t> attachPid;
  std::wstring smokePath;
  std::wstring diagnosticPath;
  bool smokeExit = false;
  bool diagnosticExit = false;
};

bool parseOptions(int argc, wchar_t **argv, StartupOptions &options) {
  for (int i = 1; i < argc; ++i) {
    const std::wstring option = argv[i];
    const auto next = [&](std::wstring &value) {
      if (i + 1 >= argc)
        return false;
      value = argv[++i];
      return true;
    };
    if (option == L"--attach-pid") {
      std::wstring value;
      if (!next(value))
        return false;
      try {
        const auto parsed = std::stoul(value);
        if (!parsed || parsed > 0xFFFFFFFFull)
          return false;
        options.attachPid = uint32_t(parsed);
      } catch (...) {
        return false;
      }
    } else if (option == L"--startup-smoke-output") {
      if (!next(options.smokePath))
        return false;
    } else if (option == L"--startup-smoke-exit") {
      options.smokeExit = true;
    } else if (option == L"--diagnostic-output") {
      if (!next(options.diagnosticPath))
        return false;
    } else if (option == L"--diagnostic-exit") {
      options.diagnosticExit = true;
    }
  }
  if (!options.diagnosticPath.empty() && !options.attachPid)
    return false;
  if (options.diagnosticExit && options.diagnosticPath.empty())
    return false;
  if (options.smokeExit && options.smokePath.empty())
    return false;
  if (!options.smokePath.empty() && options.attachPid)
    return false;
  return true;
}

HFONT makeFont(HDC dc, int pointSize) {
  LOGFONTW font{};
  font.lfHeight = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
  font.lfCharSet = GB2312_CHARSET;
  font.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(font.lfFaceName, L"微软雅黑");
  return CreateFontIndirectW(&font);
}

const wchar_t *windowClass() { return L"BanshouNativeWindow"; }

}  // namespace

Win32MainWindow::Win32MainWindow(HINSTANCE instance, bool diagnostic)
    : instance_(instance), diagnostic_(diagnostic) {}

Win32MainWindow::~Win32MainWindow() {
  for (HFONT font : fonts_)
    DeleteObject(font);
}

HWND Win32MainWindow::control(const wchar_t *name) const {
  const auto found = byName_.find(name);
  return found == byName_.end() ? nullptr : found->second;
}

void Win32MainWindow::setStatus(const std::wstring &text) {
  statusText_ = text;
  if (HWND label = control(L"label_window2"))
    SetWindowTextW(label, text.c_str());
}

void Win32MainWindow::showPage(int page) {
  if (page < 0 || page >= 8)
    return;
  currentPage_ = page;
  for (const auto &entry : kControls) {
    if (entry.kind == CTL_PAGE && entry.id >= 1000) {
      const HWND panel = control(entry.name);
      if (panel)
        ShowWindow(panel, entry.page == page ? SW_SHOW : SW_HIDE);
    }
  }
  for (int i = 0; i < 8; ++i)
    SendMessageW(toolButtons_[i], BM_SETCHECK, i == page ? BST_CHECKED : BST_UNCHECKED, 0);
  if (business_ && memory_)
    business_->refreshCurrentPage(page);
  if (HWND title = control(L"label_title")) {
    static const wchar_t *names[] = {L"控制", L"人物", L"战场", L"仓库",
                                     L"天赋", L"变量", L"自定", L"必杀"};
    SetWindowTextW(title, names[page]);
  }
}

bool Win32MainWindow::create() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
  wc.lpszClassName = windowClass();
  RegisterClassExW(&wc);

  // Qt's setFixedSize(700,530) fixes the CLIENT area.  Compute the outer size.
  constexpr long clientWidth = 700, clientHeight = 530;
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT outer{0, 0, clientWidth, clientHeight};
  AdjustWindowRectEx(&outer, style, FALSE, 0);
  hwnd_ = CreateWindowExW(0, windowClass(), L"曹操传通用调试器 by Puluo", style,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          outer.right - outer.left, outer.bottom - outer.top,
                          nullptr, nullptr, instance_, this);
  if (!hwnd_)
    return false;

  HDC dc = GetDC(hwnd_);
  HFONT defaultFont = makeFont(dc, 9);
  fonts_.push_back(defaultFont);
  for (int size : {8, 10}) {
    HFONT font = makeFont(dc, size);
    fonts_.push_back(font);
    fontBySize_[size] = font;
  }
  ReleaseDC(hwnd_, dc);

  // Pass 1: create the 9 page panels (widget_0..widget_8) first.  Every page
  // control will be a real child of its panel, so show/hide of one HWND
  // reproduces the Qt page switching exactly.
  constexpr int frameX = 9, frameY = 77;
  for (const auto &entry : kControls) {
    if (entry.kind != CTL_PAGE)
      continue;
    HWND panel = CreateWindowExW(0, L"STATIC", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS,
                                 frameX + entry.x, frameY + entry.y,
                                 entry.w, entry.h, hwnd_,
                                 reinterpret_cast<HMENU>(static_cast<intptr_t>(entry.id)),
                                 instance_, nullptr);
    controls_.push_back(panel);
    byName_[entry.name] = panel;
    if (entry.name[7] >= L'1' && entry.name[7] <= L'8')
      pages_[entry.name[7] - L'1'] = panel;
  }

  for (const auto &entry : kControls) {
    if (entry.kind == CTL_PAGE)
      continue;
    const wchar_t *className = nullptr;
    DWORD styleBits = WS_CHILD | WS_VISIBLE;
    switch (entry.kind) {
    case CTL_LABEL:
      className = L"STATIC";
      styleBits |= SS_LEFT;
      if (entry.align == 1) styleBits = (styleBits & ~SS_LEFT) | SS_RIGHT;
      if (entry.align == 2) styleBits = (styleBits & ~SS_LEFT) | SS_CENTER;
      break;
    case CTL_EDIT:
      className = L"EDIT";
      styleBits |= WS_BORDER | ES_AUTOHSCROLL;
      break;
    case CTL_COMBO:
      className = L"COMBOBOX";
      styleBits |= CBS_DROPDOWNLIST | WS_VSCROLL;
      break;
    case CTL_BUTTON:
      className = L"BUTTON";
      styleBits |= BS_PUSHBUTTON;
      break;
    case CTL_CHECK:
      className = L"BUTTON";
      styleBits |= BS_AUTOCHECKBOX;
      break;
    case CTL_RADIO:
      className = L"BUTTON";
      styleBits |= BS_AUTORADIOBUTTON;
      break;
    case CTL_LIST:
      className = L"LISTBOX";
      styleBits |= LBS_NOTIFY | WS_BORDER | WS_VSCROLL | LBS_HASSTRINGS;
      break;
    case CTL_TABLE:
      className = WC_LISTVIEW;
      styleBits |= LVS_REPORT | LVS_SINGLESEL | LVS_EDITLABELS | WS_BORDER;
      break;
    default:
      className = L"STATIC";
      break;
    }

    HWND parent = hwnd_;
    if (entry.page >= 0 && entry.page <= 7 && pages_[entry.page])
      parent = pages_[entry.page];
    HWND child = CreateWindowExW(0, className, entry.text, styleBits,
                                 entry.x, entry.y, entry.w, entry.h, parent,
                                 reinterpret_cast<HMENU>(static_cast<intptr_t>(entry.id)),
                                 instance_, nullptr);
    if (!child)
      continue;
    const HFONT font = entry.pointSize == 9 ? defaultFont
                                            : fontBySize_[entry.pointSize];
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    controls_.push_back(child);
    byName_[entry.name] = child;
    if (!entry.visible)
      ShowWindow(child, SW_HIDE);
  }

  for (size_t i = 0; i < kWinToolCount; ++i) {
    const auto &tool = kWinTools[i];
    toolButtons_[i] = CreateWindowExW(0, L"BUTTON", tool.text,
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                      tool.x, tool.y, tool.w, tool.h, hwnd_,
                                      reinterpret_cast<HMENU>(static_cast<intptr_t>(tool.id)),
                                      instance_, nullptr);
    SendMessageW(toolButtons_[i], WM_SETFONT,
                 reinterpret_cast<WPARAM>(defaultFont), TRUE);
  }

  business_ = std::make_unique<Win32Business>(*this);
  wireBusiness();
  business_->populateCatalogs();

  // 三级查找 DIY.csv：exe 目录 → 用户数据目录 → 当前工作目录。
  const auto appDiy = exePath().substr(0, exePath().find_last_of(L"\\/")) +
                      L"\\DIY.csv";
  std::wstring userDiy;
  wchar_t localPath[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                 localPath))) {
    userDiy = std::wstring(localPath) + L"\\CaoCaoToolbox\\扳手\\DIY.csv";
  }
  std::wstring chosen;
  for (const auto &candidate :
       {appDiy, userDiy, std::wstring(L"DIY.csv")}) {
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
      chosen = candidate;
      break;
    }
  }
  if (!chosen.empty()) {
    if (!userDiy.empty() && chosen != userDiy &&
        GetFileAttributesW(userDiy.c_str()) == INVALID_FILE_ATTRIBUTES) {
      std::wstring directory = userDiy.substr(0, userDiy.find_last_of(L"\\"));
      SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
      CopyFileW(chosen.c_str(), userDiy.c_str(), FALSE);
    }
    diyPath_ = chosen;
  }

  showPage(0);
  return true;
}

void Win32MainWindow::toggleControl(int index) {
  auto session = memory_;
  if (!session)
    return;
  const wchar_t *name = (std::wstring(L"checkBox_") +
                         std::to_wstring(index + 1)).c_str();
  const bool on = checked(name);
  if (index == 6) {
    LegacyAutoReturnPatch::set(session, profile_, on);
    return;
  }
  std::vector<ResolvedPatch> patches;
  try {
    RuntimeModuleImage image(session->imagePath, session);
    patches = ProfileRegistry().resolvePatches(image, kNativePatchKeys[index]);
  } catch (const std::exception &error) {
    setStatus(L"当前画像没有该控制项的配方：" + wide(error.what()));
    setChecked(name, false);
    return;
  }
  if (patches.empty()) {
    setStatus(L"当前画像没有该控制项的签名配方");
    setChecked(name, false);
    return;
  }
  PatchManager manager(session);
  for (const auto &patch : patches) {
    const auto address = session->actual(patch.address);
    if (on)
      manager.apply(address, patch.original, patch.replacement);
    else
      manager.restore(address, patch.replacement, patch.original);
  }
}

int Win32MainWindow::idOf(const wchar_t *name) const {
  for (const auto &entry : kControls)
    if (wcscmp(entry.name, name) == 0)
      return entry.id;
  return 0;
}

void Win32MainWindow::wireBusiness() {
  auto *business = business_.get();
  commandHandler_ = [business, this](int id, HWND, WORD notification) {
    if (notification != BN_CLICKED && notification != CBN_SELCHANGE &&
        notification != LBN_SELCHANGE)
      return;
    const auto fire = [&](const wchar_t *name, auto &&action) {
      if (id == idOf(name)) {
        try { action(); }
        catch (const std::exception &error) { setStatus(wide(error.what())); }
      }
    };
    fire(L"data_save", [&] { business->savePerson(); });
    fire(L"data_recal", [&] { business->recalculatePerson(); });
    fire(L"war_save", [&] { business->saveBattle(); });
    fire(L"war_life", [&] { business->reviveBattleUnit(); });
    fire(L"war_kill", [&] { business->killBattleArea(); });
    fire(L"war_connect", [&] { business->connectBattlePerson(); });
    fire(L"war_lockadd", [&] { business->addBattleLock(); });
    fire(L"war_locksub", [&] { business->removeBattleLock(); });
    fire(L"item_save", [&] { business->saveWarehouse(); });
    fire(L"item_all_1", [&] { business->warehouseBulk(1); });
    fire(L"item_all_2", [&] { business->warehouseBulk(2); });
    fire(L"item_all_3", [&] { business->warehouseBulk(3); });
    fire(L"item_save_2", [&] { business->saveEffect(); });
    fire(L"var_input_4", [&] { business->refreshVariables(); });
    fire(L"var_save_1", [&] { business->saveVariable(0); });
    fire(L"var_save_2", [&] { business->saveVariable(1); });
    fire(L"var_save_3", [&] { business->saveVariable(2); });
    fire(L"diy_input_2", [&] { business->saveDiyValue(); });
    fire(L"item_save_4", [&] { business->saveFatal(); });

    if (id == idOf(L"checkBox_9")) {
      if (checked(L"checkBox_9"))
        SetTimer(hwnd_, 4, 1000, nullptr);
      else
        KillTimer(hwnd_, 4);
      setStatus(checked(L"checkBox_9") ? L"我军自动复活已开启"
                                       : L"我军自动复活已关闭");
    } else if (id >= idOf(L"checkBox_1") && id <= idOf(L"checkBox_7")) {
      try { toggleControl(int(id - idOf(L"checkBox_1"))); }
      catch (const std::exception &error) { setStatus(wide(error.what())); }
    } else if (id == idOf(L"checkBox_8")) {
      const auto count = profile_.limit("battle_ours", 20) +
                         profile_.limit("battle_friends", 40);
      const auto base = profile_.address("battle_units", 0x004A7B20);
      const auto stride =
          profile_.limit("battle_stride", profile_.is66() ? 0x30 : 0x24);
      if (memory_) {
        for (uint32_t slot = 0; slot < count; ++slot) {
          const auto at = checkedAddress(base, uint64_t(slot) * stride, stride);
          if (memory_->u(at, 2) != 0xffff)
            memory_->writeU(at + 0x0e,
                            checked(L"checkBox_8") ? 1 : 7, 1, false);
        }
      }
    } else if (id == idOf(L"war_input_11")) {
      business->refreshBattleStrategy();
    } else if (id == idOf(L"diy_input_4")) {
      diySection_ = size_t(std::max(0, comboIndex(L"diy_input_4")));
      diyOffset_ = 0;
      business->refreshDiy();
    }
  };
  notifyHandler_ = [business](NMHDR *header) {
    (void)header;
  };
  timerHandler_ = [business, this](UINT_PTR timer) {
    if (timer == 3) {
      try { business->runBattleLocks(); }
      catch (const std::exception &) {}
    } else if (timer == 4) {
      try { business->runAutoRevive(); }
      catch (const std::exception &error) {
        KillTimer(hwnd_, timer);
        setChecked(L"checkBox_9", false);
        setStatus(wide(error.what()));
      }
    }
  };
  contextMenuHandler_ = [business](HWND source, POINT point) {
    const int id = GetDlgCtrlID(source);
    if (id == 1014 || id == 1312)
      business->showBooleanMenu(source, point);
  };
}

std::wstring Win32MainWindow::text(const wchar_t *name) const {
  const HWND handle = control(name);
  if (!handle)
    return {};
  const int length = GetWindowTextLengthW(handle);
  std::wstring value(size_t(length), L'\0');
  if (length > 0)
    GetWindowTextW(handle, value.data(), length + 1);
  return value;
}

void Win32MainWindow::setText(const wchar_t *name, const std::wstring &value) {
  if (HWND handle = control(name))
    SetWindowTextW(handle, value.c_str());
}

void Win32MainWindow::setText(const wchar_t *name, uint64_t value) {
  setText(name, std::to_wstring(value));
}

uint64_t Win32MainWindow::number(const wchar_t *name, uint64_t maximum,
                                 const char *) const {
  try {
    const std::wstring value = text(name);
    if (value.empty())
      return 0;
    size_t used = 0;
    const auto parsed = std::stoull(value, &used, 10);
    return used == value.size() ? std::min(parsed, maximum) : 0;
  } catch (...) {
    return 0;
  }
}

int32_t Win32MainWindow::signedNumber(const wchar_t *name, const char *) const {
  try {
    return int32_t(std::stoll(text(name), nullptr, 10));
  } catch (...) {
    return 0;
  }
}

bool Win32MainWindow::checked(const wchar_t *name) const {
  const HWND handle = control(name);
  return handle && SendMessageW(handle, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void Win32MainWindow::setChecked(const wchar_t *name, bool value) {
  if (HWND handle = control(name)) {
    suppressNotifications_ = true;
    SendMessageW(handle, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    suppressNotifications_ = false;
  }
}

int Win32MainWindow::comboIndex(const wchar_t *name) const {
  const HWND handle = control(name);
  return handle ? int(SendMessageW(handle, CB_GETCURSEL, 0, 0)) : -1;
}

void Win32MainWindow::setComboIndex(const wchar_t *name, int index) {
  if (HWND handle = control(name)) {
    suppressNotifications_ = true;
    SendMessageW(handle, CB_SETCURSEL, WPARAM(index), 0);
    suppressNotifications_ = false;
  }
}

uint64_t Win32MainWindow::comboData(const wchar_t *name,
                                    uint64_t fallback) const {
  const int index = comboIndex(name);
  if (index < 0)
    return fallback;
  const HWND handle = control(name);
  const LPARAM value = SendMessageW(handle, CB_GETITEMDATA, WPARAM(index), 0);
  return value == CB_ERR ? fallback : uint64_t(value);
}

bool Win32MainWindow::selectComboData(const wchar_t *name, uint64_t value) {
  const HWND handle = control(name);
  if (!handle)
    return false;
  const int count = int(SendMessageW(handle, CB_GETCOUNT, 0, 0));
  for (int index = 0; index < count; ++index) {
    const LPARAM item = SendMessageW(handle, CB_GETITEMDATA, WPARAM(index), 0);
    if (item != CB_ERR && uint64_t(item) == value) {
      setComboIndex(name, index);
      return true;
    }
  }
  setComboIndex(name, 0);
  return false;
}

int Win32MainWindow::addCombo(const wchar_t *name, const std::wstring &label,
                              uint64_t data) {
  const HWND handle = control(name);
  if (!handle)
    return -1;
  const int index = int(SendMessageW(handle, CB_ADDSTRING, 0,
                                     reinterpret_cast<LPARAM>(label.c_str())));
  SendMessageW(handle, CB_SETITEMDATA, WPARAM(index), LPARAM(data));
  return index;
}

void Win32MainWindow::clearControl(const wchar_t *name) {
  const HWND handle = control(name);
  if (!handle)
    return;
  wchar_t className[64] = {};
  GetClassNameW(handle, className, 64);
  if (wcscmp(className, L"ComboBox") == 0 ||
      wcscmp(className, L"ListBox") == 0)
    SendMessageW(handle, CB_RESETCONTENT, 0, 0);
  else if (wcscmp(className, WC_LISTVIEW) == 0)
    SendMessageW(handle, LVM_DELETEALLITEMS, 0, 0);
  else
    SetWindowTextW(handle, L"");
}

int Win32MainWindow::listCount(const wchar_t *name) const {
  const HWND handle = control(name);
  return handle ? int(SendMessageW(handle, LB_GETCOUNT, 0, 0)) : 0;
}

int Win32MainWindow::listSelection(const wchar_t *name) const {
  const HWND handle = control(name);
  return handle ? int(SendMessageW(handle, LB_GETCURSEL, 0, 0)) : -1;
}

uint64_t Win32MainWindow::listData(const wchar_t *name, int row) const {
  const HWND handle = control(name);
  if (!handle || row < 0)
    return 0;
  const LPARAM value = SendMessageW(handle, LB_GETITEMDATA, WPARAM(row), 0);
  return value == LB_ERR ? 0 : uint64_t(value);
}

void Win32MainWindow::setListData(const wchar_t *name, int row, uint64_t data) {
  if (HWND handle = control(name))
    SendMessageW(handle, LB_SETITEMDATA, WPARAM(row), LPARAM(data));
}

int Win32MainWindow::addList(const wchar_t *name, const std::wstring &label,
                             uint64_t data) {
  const HWND handle = control(name);
  if (!handle)
    return -1;
  suppressNotifications_ = true;
  const int row = int(SendMessageW(handle, LB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(label.c_str())));
  SendMessageW(handle, LB_SETITEMDATA, WPARAM(row), LPARAM(data));
  suppressNotifications_ = false;
  return row;
}

void Win32MainWindow::setEnabled(const wchar_t *name, bool enabled) {
  if (HWND handle = control(name))
    EnableWindow(handle, enabled ? TRUE : FALSE);
}

void Win32MainWindow::setVisible(const wchar_t *name, bool visible) {
  if (HWND handle = control(name))
    ShowWindow(handle, visible ? SW_SHOW : SW_HIDE);
}

void Win32MainWindow::setRect(const wchar_t *name, int x, int y, int width,
                              int height) {
  if (HWND handle = control(name))
    SetWindowPos(handle, nullptr, x, y, width, height, SWP_NOZORDER);
}

void Win32MainWindow::onCommand(int id, HWND source, WORD notification) {
  if (suppressNotifications_)
    return;
  if (id >= 9000 && id < 9008) {
    showPage(id - 9000);
    return;
  }
  if (commandHandler_)
    commandHandler_(id, source, notification);
}

void Win32MainWindow::onNotify(NMHDR *header) {
  if (!suppressNotifications_ && notifyHandler_)
    notifyHandler_(header);
}

void Win32MainWindow::onTimer(UINT_PTR timer) {
  if (timerHandler_)
    timerHandler_(timer);
}

void Win32MainWindow::onSessionState(uint64_t generation) {
  if (generation < sessionGeneration_)
    return;
  sessionGeneration_ = generation;
}

std::string Win32MainWindow::diagnosticSnapshot() const {
  Json report;
  report["schema"] = "banshou-read-only-diagnostic-v1";
  report["operation"] = "attach-eight-pages";
  report["gameMemoryWrites"] = false;
  report["remoteCalls"] = false;
  report["success"] = false;
  report["error"] = "原生界面尚未附加引擎";
  report["readOnlyEnforced"] = diagnostic_;
  report["sessionGeneration"] = sessionGeneration_;
  report["recordCounts"] = Json::object();
  report["pages"] = Json::array();
  for (int page = 0; page < 8; ++page) {
    Json item;
    item["page"] = page;
    item["success"] = currentPage_ == page;
    report["pages"].push_back(item);
  }
  return report.dump(2);
}


bool Win32MainWindow::attachPid(uint32_t pid, bool readOnly) {
  struct Context {
    DWORD pid = 0;
    HWND window = nullptr;
  } context{pid, nullptr};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto *context = reinterpret_cast<Context *>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != context->pid || !IsWindowVisible(window))
          return TRUE;
        wchar_t title[256] = {};
        GetWindowTextW(window, title, 256);
        if (!title[0])
          return TRUE;
        context->window = window;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&context));
  if (!context.window)
    throw Error("找不到该 PID 对应的可见窗口");

  const auto info = locateProcess(context.window);
  auto backend = std::make_shared<Win32MemoryBackend>(info.pid, readOnly);
  auto session =
      std::make_shared<MemorySession>(backend, info.base, info.size, info.pid);
  session->imagePath = info.path;
  session->readOnly = readOnly;

  ProfileRegistry registry;
  auto profile = registry.detect(info.path, -1);
  try {
    RuntimeModuleImage runtime(info.path, session);
    registry.enrichLegacyDirect(profile, runtime);
  } catch (const std::exception &error) {
    setStatus(L"运行时画像补充失败：" + wide(error.what()));
  }
  memory_ = session;
  profile_ = profile;
  setStatus(L"已附加游戏进程 " + std::to_wstring(pid));
  if (business_) {
    business_->populateCatalogs();
    business_->refreshCurrentPage(currentPage_);
  }
  return true;
}

namespace {
std::wstring nativeGameText(const std::shared_ptr<MemorySession> &session,
                            uint32_t address, size_t width) {
  const auto bytes = session->read(address, width);
  const auto end = std::find(bytes.begin(), bytes.end(), uint8_t(0));
  if (end == bytes.begin())
    return L"";
  const int count = int(end - bytes.begin());
  for (UINT codePage : {UINT(54936), UINT(936)}) {
    const int needed = MultiByteToWideChar(
        codePage, MB_ERR_INVALID_CHARS,
        reinterpret_cast<LPCCH>(bytes.data()), count, nullptr, 0);
    if (!needed)
      continue;
    std::wstring out(size_t(needed), L'\0');
    MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                        reinterpret_cast<LPCCH>(bytes.data()), count,
                        out.data(), needed);
    return out;
  }
  return L"名称解码失败";
}

std::wstring nativeItemName(const std::shared_ptr<MemorySession> &session,
                            const EngineProfile &profile, uint16_t id) {
  const auto table = profile.address("item_table", 0);
  if (!table)
    return L"物品" + std::to_wstring(id);
  try {
    return nativeGameText(
        session,
        checkedAddress(table, uint64_t(id) * 25,
                       profile.limit("item_name_size", 12)),
        profile.limit("item_name_size", 12));
  } catch (...) {
    return L"物品" + std::to_wstring(id);
  }
}

}  // namespace

LRESULT CALLBACK Win32MainWindow::proc(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam) {
  Win32MainWindow *self = nullptr;
  if (message == WM_NCCREATE) {
    auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
    self = static_cast<Win32MainWindow *>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<Win32MainWindow *>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self) {
    switch (message) {
    case WM_GETMINMAXINFO: {
      auto *info = reinterpret_cast<MINMAXINFO *>(lparam);
      RECT outer{0, 0, 700, 530};
      AdjustWindowRectEx(&outer,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                         FALSE, 0);
      info->ptMinTrackSize = {outer.right - outer.left, outer.bottom - outer.top};
      info->ptMaxTrackSize = info->ptMinTrackSize;
      return 0;
    }
    case WM_COMMAND:
      self->onCommand(LOWORD(wparam), reinterpret_cast<HWND>(lparam),
                      HIWORD(wparam));
      return 0;
    case WM_NOTIFY:
      self->onNotify(reinterpret_cast<NMHDR *>(lparam));
      return 0;
    case WM_CONTEXTMENU:
      if (self->contextMenuHandler_) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        self->contextMenuHandler_(reinterpret_cast<HWND>(wparam), point);
        return 0;
      }
      break;
    case WM_TIMER:
      self->onTimer(static_cast<UINT_PTR>(wparam));
      return 0;
    case kSessionStateMessage:
      self->onSessionState(static_cast<uint64_t>(wparam));
      return 0;
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

int runWindow(int argc, wchar_t **argv) {
  StartupOptions options;
  if (!parseOptions(argc, argv, options))
    return 2;

  INITCOMMONCONTROLSEX commonControls{};
  commonControls.dwSize = sizeof(commonControls);
  commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&commonControls);

  Win32MainWindow window(GetModuleHandleW(nullptr),
                         !options.diagnosticPath.empty());
  if (!window.create())
    return 5;
  ShowWindow(window.handle(), SW_SHOW);
  UpdateWindow(window.handle());

  if (!options.smokePath.empty()) {
    SetTimer(window.handle(), kSmokeTimer, 250, nullptr);
  } else if (options.attachPid) {
    try {
      window.attachPid(*options.attachPid, !options.diagnosticPath.empty());
    } catch (const std::exception &error) {
      window.setStatus(wide(error.what()));
    }
    if (!options.diagnosticPath.empty())
      SetTimer(window.handle(), kDiagnosticTimer, 3000, nullptr);
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    if (message.message == WM_TIMER &&
        message.wParam == kSmokeTimer) {
      KillTimer(window.handle(), kSmokeTimer);
      Json report;
      const bool ok = IsWindow(window.handle()) &&
                      IsWindowVisible(window.handle());
      report["schema"] = "banshou-startup-smoke-v1";
      report["success"] = ok;
      report["gameMemoryWrites"] = false;
      report["remoteCalls"] = false;
      report["windowCreated"] = ok;
      report["qtPlatform"] = "windows";
      report["pages"] = Json::array();
      for (int page = 1; page <= 8; ++page)
        report["pages"].push_back("widget_" + std::to_string(page));
      int code = ok ? 0 : 5;
      try {
        writeText(options.smokePath, report.dump(2) + "\n");
      } catch (...) {
        code = 5;
      }
      if (options.smokeExit || code)
        PostQuitMessage(code);
      continue;
    }
    if (message.message == WM_TIMER &&
        message.wParam == kDiagnosticTimer) {
      KillTimer(window.handle(), kDiagnosticTimer);
      Json report = Json::parse(window.diagnosticSnapshot());
      const int code = report.value("success", false) ? 0 : 2;
      try {
        writeText(options.diagnosticPath, report.dump(2) + "\n");
      } catch (...) {
        PostQuitMessage(3);
        continue;
      }
      if (options.diagnosticExit || code)
        PostQuitMessage(code);
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

}  // namespace banshou
