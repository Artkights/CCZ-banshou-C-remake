#include "win32_mainwindow.hpp"
#include "platform.hpp"
#include "game.hpp"
#include "image.hpp"
#include "win32_business.hpp"

#include "win32_control_table.inc"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>
#include <algorithm>
#include <cstdlib>
#include <json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace banshou {
namespace {

constexpr UINT kSessionStateMessage = WM_APP + 1;
constexpr UINT_PTR kSmokeTimer = 0xB001;
constexpr UINT_PTR kDiagnosticTimer = 0xB002;
static_assert(kSmokeTimer != 3 && kSmokeTimer != 4,
              "smoke timer must not collide with the periodic timers");
constexpr int kClientWidth = 700;
constexpr int kClientHeight = 530;
constexpr int kFrameX = 9;
constexpr int kFrameY = 77;
constexpr int kFrameWidth = 681;
constexpr int kFrameHeight = 389;
constexpr int kCentralPage = 255;
constexpr int kHiddenPage = 8;
constexpr int kFrameId = 1500;
constexpr UINT_PTR kFrameSubclass = 1;
constexpr UINT_PTR kPanelSubclass = 2;
constexpr UINT_PTR kCellSubclass = 3;
constexpr COLORREF kTransparent = 0xFFFFFFFFu;
constexpr int kToolPadding = 8;
constexpr int kToolGap = 2;

const char *const kPatchKeys[] = {
    "control_friendly", "control_all_factions", "control_no_ai",
    "control_move_after_wait", "control_move_255", "control_pass_through",
    "control_auto_return"};

struct WinStyleDesc {
  const wchar_t *name;
  COLORREF text;
  COLORREF back;
  bool sunken;
  const wchar_t *face;
  int pointSize;
  bool centre;
  int imageId;
};

const WinStyleDesc kStyles[] = {
    {L"label_title", RGB(0, 0, 0), kTransparent, false, nullptr, 0, true, 0},
    {L"label_pivot", RGB(0, 0, 0), RGB(255, 255, 255), true, nullptr, 0, false,
     105},
    {L"label_window1", RGB(0, 255, 0), RGB(5, 5, 5), true, nullptr, 0, false, 0},
    {L"label_window2", RGB(0, 255, 0), RGB(5, 5, 5), true, nullptr, 0, false, 0},
    {L"label_help", RGB(255, 0, 4), kTransparent, false, L"黑体", 9, false, 0},
    {L"label", RGB(0, 0, 0), RGB(253, 255, 244), false, nullptr, 0, false, 0},
};

struct WinTableDesc {
  const wchar_t *name;
  int columns;
  const wchar_t *titles[6];
  int widths[6];
};

const WinTableDesc kTables[] = {
    {L"listview_var_1",
     3,
     {L"布尔", L"值", L"地址", nullptr, nullptr, nullptr},
     {60, 55, 95, 0, 0, 0}},
    {L"listview_var_2",
     3,
     {L"整型", L"值", L"地址", nullptr, nullptr, nullptr},
     {45, 75, 90, 0, 0, 0}},
    {L"listview_var_3",
     3,
     {L"指针", L"值", L"地址", nullptr, nullptr, nullptr},
     {45, 75, 90, 0, 0, 0}},
    {L"listview_diy",
     6,
     {L"变量名", L"值", L"值解释", L"地址", L"字节数", L"注释"},
     {100, 80, 90, 70, 45, 230}},
};

const WinStyleDesc *styleOf(const wchar_t *name) {
  for (const auto &style : kStyles)
    if (wcscmp(style.name, name) == 0)
      return &style;
  return nullptr;
}

const WinTableDesc *tableOf(const wchar_t *name) {
  for (const auto &table : kTables)
    if (wcscmp(table.name, name) == 0)
      return &table;
  return nullptr;
}

HFONT makeFont(HDC dc, int pointSize, const wchar_t *face = nullptr) {
  LOGFONTW font{};
  font.lfHeight = -MulDiv(pointSize, GetDeviceCaps(dc, LOGPIXELSY), 72);
  font.lfCharSet = GB2312_CHARSET;
  font.lfQuality = CLEARTYPE_QUALITY;
  wcscpy_s(font.lfFaceName, face ? face : L"微软雅黑");
  return CreateFontIndirectW(&font);
}

const wchar_t *windowClass() { return L"BanshouNativeWindow"; }

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

bool isClass(HWND hwnd, const wchar_t *name) {
  if (!hwnd)
    return false;
  wchar_t buffer[64] = {};
  GetClassNameW(hwnd, buffer, 63);
  return wcscmp(buffer, name) == 0;
}

}  // namespace

Win32MainWindow::Win32MainWindow(HINSTANCE instance, bool diagnostic)
    : instance_(instance), diagnostic_(diagnostic) {}

Win32MainWindow::~Win32MainWindow() {
  commandHandler_ = nullptr;
  notifyHandler_ = nullptr;
  timerHandler_ = nullptr;
  contextMenuHandler_ = nullptr;
  business_.reset();
  if (hwnd_ && IsWindow(hwnd_))
    DestroyWindow(hwnd_);
  for (HFONT font : fonts_)
    DeleteObject(font);
  for (auto &entry : brushes_)
    DeleteObject(entry.second);
  if (faceBrush_)
    DeleteObject(faceBrush_);
}

HWND Win32MainWindow::control(const wchar_t *name) const {
  const auto found = byName_.find(name);
  return found == byName_.end() ? nullptr : found->second;
}

HWND Win32MainWindow::pagePanel(int page) const {
  if (page < 0 || page > 8)
    return nullptr;
  return pages_[page];
}

HWND Win32MainWindow::toolButton(int index) const {
  return index >= 0 && index < 8 ? toolButtons_[index] : nullptr;
}

int Win32MainWindow::idOf(const wchar_t *name) const {
  for (const auto &entry : kControls)
    if (wcscmp(entry.name, name) == 0)
      return entry.id;
  return 0;
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
  for (int i = 0; i < 9; ++i) {
    if (pages_[i])
      ShowWindow(pages_[i], i == page ? SW_SHOW : SW_HIDE);
  }
  for (int i = 0; i < 8; ++i) {
    if (toolButtons_[i])
      InvalidateRect(toolButtons_[i], nullptr, TRUE);
  }
  if (business_ && memory_)
    business_->refreshCurrentPage(page);
  if (HWND title = control(L"label_title")) {
    static const wchar_t *names[] = {L"控制", L"人物", L"战场", L"仓库",
                                     L"天赋", L"变量", L"自定", L"必杀"};
    SetWindowTextW(title, names[page]);
  }
}

void Win32MainWindow::applyInitialState() {
  setChecked(L"version_1", true);
  setChecked(L"war_kind_1", true);
  for (int i = 1; i <= 3; ++i) {
    const std::wstring name = L"var_input_" + std::to_wstring(i);
    setText(name.c_str(), L"0");
  }
  setText(L"war_input_47", L"0");
  setText(L"war_input_48", L"0");
  setText(L"war_input_49", L"255");
  setText(L"war_input_50", L"255");
  for (int i = 0; i < 8; ++i)
    if (toolButtons_[i])
      EnableWindow(toolButtons_[i], FALSE);
  if (pages_[0])
    EnableWindow(pages_[0], FALSE);
}

bool Win32MainWindow::create() {
  try {
    return createImpl();
  } catch (const std::exception &error) {
    startupError_ = wide(error.what());
    log(std::string("create failed: ") + error.what());
    return false;
  } catch (...) {
    startupError_ = L"未知异常";
    log("create failed: unknown exception");
    return false;
  }
}

bool Win32MainWindow::createImpl() {
  faceBrush_ = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = proc;
  wc.hInstance = instance_;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(101));
  wc.lpszClassName = windowClass();
  wc.hbrBackground = faceBrush_;
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    startupError_ = L"窗口类注册失败";
    return false;
  }

  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT outer{0, 0, kClientWidth, kClientHeight};
  AdjustWindowRectEx(&outer, style, FALSE, 0);
  hwnd_ = CreateWindowExW(0, windowClass(), L"曹操传通用调试器 by Puluo", style,
                          CW_USEDEFAULT, CW_USEDEFAULT,
                          outer.right - outer.left, outer.bottom - outer.top,
                          nullptr, nullptr, instance_, this);
  if (!hwnd_)
    return false;

  HDC dc = GetDC(hwnd_);
  defaultFont_ = makeFont(dc, 9);
  fonts_.push_back(defaultFont_);
  for (int size : {8, 10}) {
    HFONT font = makeFont(dc, size);
    fonts_.push_back(font);
    fontBySize_[size] = font;
  }
  TEXTMETRICW metrics{};
  SelectObject(dc, defaultFont_);
  GetTextMetricsW(dc, &metrics);
  ReleaseDC(hwnd_, dc);

  const int buttonHeight = metrics.tmHeight + 8;
  toolbarHeight_ = buttonHeight + 6;

  createFrame();
  createControls();
  createTools();

  business_ = std::make_unique<Win32Business>(*this);
  wireBusiness();
  business_->populateCatalogs();
  applyInitialState();

  // DIY.csv: exe directory -> user data directory -> working directory.
  const auto executable = exePath();
  const auto appDiy = executable.substr(0, executable.find_last_of(L"\\/")) +
                      L"\\DIY.csv";
  std::wstring userDiy;
  wchar_t localPath[MAX_PATH] = {};
  if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0,
                                 localPath))) {
    userDiy = std::wstring(localPath) + L"\\CaoCaoToolbox\\扳手\\DIY.csv";
  }
  std::wstring chosen;
  for (const auto &candidate : {appDiy, userDiy, std::wstring(L"DIY.csv")}) {
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
      chosen = candidate;
      break;
    }
  }
  if (!chosen.empty()) {
    if (!userDiy.empty() && chosen != userDiy &&
        GetFileAttributesW(userDiy.c_str()) == INVALID_FILE_ATTRIBUTES) {
      const std::wstring directory = userDiy.substr(0, userDiy.find_last_of(L"\\"));
      SHCreateDirectoryExW(nullptr, directory.c_str(), nullptr);
      CopyFileW(chosen.c_str(), userDiy.c_str(), FALSE);
    }
    diyPath_ = chosen;
  }

  showPage(0);
  return true;
}

void Win32MainWindow::createFrame() {
  frame_ = CreateWindowExW(0, L"STATIC", L"",
                           WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
                           kFrameX, kFrameY + toolbarHeight_, kFrameWidth,
                           kFrameHeight, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<intptr_t>(kFrameId)),
                           instance_, nullptr);
  byName_[L"frame"] = frame_;
  SetWindowSubclass(frame_, containerProc, kFrameSubclass,
                    reinterpret_cast<DWORD_PTR>(this));
}

void Win32MainWindow::createControls() {
  for (const auto &entry : kControls) {
    if (entry.kind != CTL_PAGE)
      continue;
    const HWND parent = frame_ ? frame_ : hwnd_;
    HWND panel = CreateWindowExW(0, L"STATIC", L"",
                                 WS_CHILD | WS_CLIPSIBLINGS, entry.x, entry.y,
                                 entry.w, entry.h, parent,
                                 reinterpret_cast<HMENU>(static_cast<intptr_t>(entry.id)),
                                 instance_, nullptr);
    if (!panel)
      continue;
    controls_.push_back(panel);
    byName_[entry.name] = panel;
    byId_[entry.id] = &entry;
    if (entry.page >= 0 && entry.page <= kHiddenPage)
      pages_[entry.page] = panel;
    SetWindowSubclass(panel, containerProc, kPanelSubclass,
                      reinterpret_cast<DWORD_PTR>(this));
  }

  for (const auto &entry : kControls) {
    if (entry.kind == CTL_PAGE)
      continue;
    const WinStyleDesc *style = styleOf(entry.name);
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
      styleBits |= LVS_REPORT | LVS_SINGLESEL | WS_BORDER;
      break;
    default:
      className = L"STATIC";
      break;
    }
    if (style) {
      if (style->sunken)
        styleBits |= SS_SUNKEN;
      if (style->centre)
        styleBits = (styleBits & ~SS_LEFT) | SS_CENTER;
      if (style->imageId)
        styleBits |= SS_BITMAP | SS_CENTERIMAGE;
    }

    HWND parent = hwnd_;
    int x = entry.x;
    int y = entry.y;
    if (entry.page >= 0 && entry.page <= kHiddenPage && pages_[entry.page])
      parent = pages_[entry.page];
    else
      y += toolbarHeight_;

    HWND child = CreateWindowExW(0, className, entry.text, styleBits, x, y,
                                 entry.w, entry.h, parent,
                                 reinterpret_cast<HMENU>(static_cast<intptr_t>(entry.id)),
                                 instance_, nullptr);
    if (!child)
      continue;
    const int pointSize =
        style && style->pointSize ? style->pointSize : entry.pointSize;
    HFONT font = pointSize == 9 ? defaultFont_ : nullptr;
    if (!font) {
      const auto found = fontBySize_.find(pointSize);
      font = found == fontBySize_.end() ? defaultFont_ : found->second;
    }
    if (style && style->face) {
      HDC dc = GetDC(child);
      font = makeFont(dc, pointSize, style->face);
      ReleaseDC(child, dc);
      fonts_.push_back(font);
    }
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (style && style->imageId) {
      HBITMAP bitmap = static_cast<HBITMAP>(LoadImageW(
          instance_, MAKEINTRESOURCEW(style->imageId), IMAGE_BITMAP, 0, 0,
          LR_DEFAULTCOLOR));
      if (bitmap)
        SendMessageW(child, STM_SETIMAGE, IMAGE_BITMAP,
                     reinterpret_cast<LPARAM>(bitmap));
    }
    if (entry.kind == CTL_TABLE) {
      ListView_SetExtendedListViewStyle(
          child, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
      if (const WinTableDesc *table = tableOf(entry.name)) {
        for (int column = 0; column < table->columns; ++column) {
          LVCOLUMNW info{};
          info.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
          info.fmt = LVCFMT_LEFT;
          info.cx = table->widths[column];
          info.pszText = const_cast<wchar_t *>(table->titles[column]);
          SendMessageW(child, LVM_INSERTCOLUMNW, WPARAM(column),
                       reinterpret_cast<LPARAM>(&info));
        }
      }
    }
    controls_.push_back(child);
    byName_[entry.name] = child;
    byId_[entry.id] = &entry;
    if (!entry.visible)
      ShowWindow(child, SW_HIDE);
  }
}

void Win32MainWindow::createTools() {
  HDC dc = GetDC(hwnd_);
  HGDIOBJ previous = SelectObject(dc, defaultFont_);
  int x = kFrameX;
  const int height = toolbarHeight_ - 6;
  const int y = 3;
  for (size_t i = 0; i < kWinToolCount; ++i) {
    SIZE extent{};
    GetTextExtentPoint32W(dc, kWinTools[i].text,
                          int(wcslen(kWinTools[i].text)), &extent);
    const int width = extent.cx + kToolPadding * 2;
    toolButtons_[i] = CreateWindowExW(
        0, L"BUTTON", kWinTools[i].text,
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, x, y, width, height, hwnd_,
        reinterpret_cast<HMENU>(static_cast<intptr_t>(kWinTools[i].id)),
        instance_, nullptr);
    SendMessageW(toolButtons_[i], WM_SETFONT,
                 reinterpret_cast<WPARAM>(defaultFont_), TRUE);
    x += width + kToolGap;
  }
  SelectObject(dc, previous);
  ReleaseDC(hwnd_, dc);
}

void Win32MainWindow::paintTool(DRAWITEMSTRUCT *item) {
  if (!item || item->CtlID < 9000 || item->CtlID >= 9000 + int(kWinToolCount))
    return;
  FillRect(item->hDC, &item->rcItem, faceBrush_);
  const bool active = currentPage_ == int(item->CtlID - 9000);
  if (active || (item->itemState & ODS_SELECTED))
    DrawEdge(item->hDC, &item->rcItem, BDR_SUNKENOUTER, BF_RECT | BF_FLAT);
  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(item->hDC, (item->itemState & ODS_DISABLED)
                              ? GetSysColor(COLOR_GRAYTEXT)
                              : RGB(0, 0, 0));
  HGDIOBJ previous = SelectObject(item->hDC, defaultFont_);
  RECT text = item->rcItem;
  DrawTextW(item->hDC, kWinTools[item->CtlID - 9000].text, -1, &text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  SelectObject(item->hDC, previous);
}

void Win32MainWindow::wireBusiness() {
  auto *business = business_.get();
  commandHandler_ = [business, this](int id, HWND, WORD notification) {
    const auto fire = [&](const wchar_t *name, auto &&action) {
      if (id != idOf(name))
        return;
      try {
        action();
      } catch (const std::exception &error) {
        setStatus(wide(error.what()));
      }
    };
    if (notification == BN_CLICKED) {
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
      fire(L"war_kind_1", [&] { business->refreshBattle(true); });
      fire(L"war_kind_2", [&] { business->refreshBattle(true); });
      fire(L"war_kind_3", [&] { business->refreshBattle(true); });

      if (id == idOf(L"checkBox_9")) {
        if (checked(L"checkBox_9"))
          SetTimer(hwnd_, 4, 1000, nullptr);
        else
          KillTimer(hwnd_, 4);
        setStatus(checked(L"checkBox_9") ? L"我军自动复活已开启"
                                         : L"我军自动复活已关闭");
        return;
      }
      if (id >= idOf(L"checkBox_1") && id <= idOf(L"checkBox_7")) {
        try {
          toggleControl(int(id - idOf(L"checkBox_1")));
        } catch (const std::exception &error) {
          setStatus(wide(error.what()));
        }
        return;
      }
      if (id == idOf(L"checkBox_8")) {
        try {
          if (!memory_)
            throw Error("尚未附加游戏");
          const auto count = profile_.limit("battle_ours", 20) +
                             profile_.limit("battle_friends", 40);
          const auto base = profile_.address("battle_units", 0x004A7B20);
          const auto stride =
              profile_.limit("battle_stride", profile_.is66() ? 0x30 : 0x24);
          const bool on = checked(L"checkBox_8");
          for (uint32_t slot = 0; slot < count; ++slot) {
            const auto at = checkedAddress(base, uint64_t(slot) * stride, stride);
            if (memory_->u(at, 2) != 0xffff)
              memory_->writeU(at + 0x0e, on ? 1 : 7, 1, false);
          }
        } catch (const std::exception &error) {
          setStatus(wide(error.what()));
        }
        return;
      }
    }
    if (notification == CBN_SELCHANGE) {
      if (id == idOf(L"war_input_11"))
        business->refreshBattleStrategy();
      else if (id == idOf(L"diy_input_4")) {
        diySection_ = size_t(std::max(0, comboIndex(L"diy_input_4")));
        diyOffset_ = 0;
        try {
          business->refreshDiy();
        } catch (const std::exception &error) {
          setStatus(wide(error.what()));
        }
      }
      return;
    }
    if (notification == LBN_SELCHANGE) {
      const auto load = [&](const wchar_t *list, auto &&action) {
        if (id == idOf(list)) {
          try {
            action();
          } catch (const std::exception &error) {
            setStatus(wide(error.what()));
          }
        }
      };
      load(L"listview_data", [&] { business->loadPerson(); });
      load(L"listview_war", [&] { business->loadBattle(); });
      load(L"listview_war_2", [&] {});
      load(L"listview_item", [&] { business->loadWarehouseRecord(); });
      load(L"listview_item_2", [&] { business->loadConsumable(); });
      load(L"listview_item_3", [&] { business->loadEffect(); });
      load(L"listview_mk", [&] { business->loadFatal(); });
      load(L"listview_diy", [&] {});
    }
  };
  notifyHandler_ = [](NMHDR *) {};
  timerHandler_ = [business, this](UINT_PTR timer) {
    if (timer == 3) {
      try {
        business->runBattleLocks();
      } catch (const std::exception &) {
      }
    } else if (timer == 4) {
      try {
        business->runAutoRevive();
      } catch (const std::exception &error) {
        KillTimer(hwnd_, timer);
        setChecked(L"checkBox_9", false);
        setStatus(wide(error.what()));
      }
    }
  };
  contextMenuHandler_ = [business, this](HWND source, POINT point) {
    const int id = GetDlgCtrlID(source);
    try {
      if (id == idOf(L"listview_var_1") || id == idOf(L"var_save_1"))
        business->showBooleanMenu(source, point);
    } catch (const std::exception &error) {
      setStatus(wide(error.what()));
    }
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
  if (isClass(handle, WC_LISTVIEW))
    SendMessageW(handle, LVM_DELETEALLITEMS, 0, 0);
  else if (isClass(handle, L"ListBox"))
    SendMessageW(handle, LB_RESETCONTENT, 0, 0);
  else if (isClass(handle, L"ComboBox"))
    SendMessageW(handle, CB_RESETCONTENT, 0, 0);
  else
    SetWindowTextW(handle, L"");
}

int Win32MainWindow::listCount(const wchar_t *name) const {
  const HWND handle = control(name);
  if (!handle)
    return 0;
  return isClass(handle, WC_LISTVIEW)
             ? int(SendMessageW(handle, LVM_GETITEMCOUNT, 0, 0))
             : int(SendMessageW(handle, LB_GETCOUNT, 0, 0));
}

int Win32MainWindow::listSelection(const wchar_t *name) const {
  const HWND handle = control(name);
  if (!handle)
    return -1;
  if (isClass(handle, WC_LISTVIEW))
    return int(SendMessageW(handle, LVM_GETNEXTITEM, WPARAM(-1),
                            LVNI_SELECTED));
  return int(SendMessageW(handle, LB_GETCURSEL, 0, 0));
}

uint64_t Win32MainWindow::listData(const wchar_t *name, int row) const {
  const HWND handle = control(name);
  if (!handle || row < 0)
    return 0;
  if (isClass(handle, WC_LISTVIEW)) {
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    if (!SendMessageW(handle, LVM_GETITEMW, 0,
                      reinterpret_cast<LPARAM>(&item)))
      return 0;
    return uint64_t(item.lParam);
  }
  const LPARAM value = SendMessageW(handle, LB_GETITEMDATA, WPARAM(row), 0);
  return value == LB_ERR ? 0 : uint64_t(value);
}

void Win32MainWindow::setListData(const wchar_t *name, int row, uint64_t data) {
  const HWND handle = control(name);
  if (!handle || row < 0)
    return;
  if (isClass(handle, WC_LISTVIEW)) {
    LVITEMW item{};
    item.mask = LVIF_PARAM;
    item.iItem = row;
    item.lParam = LPARAM(data);
    SendMessageW(handle, LVM_SETITEMW, 0, reinterpret_cast<LPARAM>(&item));
    return;
  }
  SendMessageW(handle, LB_SETITEMDATA, WPARAM(row), LPARAM(data));
}

int Win32MainWindow::addList(const wchar_t *name, const std::wstring &label,
                             uint64_t data) {
  const HWND handle = control(name);
  if (!handle)
    return -1;
  if (isClass(handle, WC_LISTVIEW)) {
    LVITEMW item{};
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = int(SendMessageW(handle, LVM_GETITEMCOUNT, 0, 0));
    item.pszText = const_cast<wchar_t *>(label.c_str());
    item.lParam = LPARAM(data);
    const int row = int(SendMessageW(handle, LVM_INSERTITEMW, 0,
                                     reinterpret_cast<LPARAM>(&item)));
    return row;
  }
  suppressNotifications_ = true;
  const int row = int(SendMessageW(handle, LB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(label.c_str())));
  SendMessageW(handle, LB_SETITEMDATA, WPARAM(row), LPARAM(data));
  suppressNotifications_ = false;
  return row;
}

int Win32MainWindow::addRow(const wchar_t *name,
                            const std::vector<std::wstring> &cells,
                            uint64_t data) {
  const HWND handle = control(name);
  if (!handle || cells.empty())
    return -1;
  if (!isClass(handle, WC_LISTVIEW)) {
    std::wstring joined;
    for (size_t i = 0; i < cells.size(); ++i) {
      if (i)
        joined += L"  ";
      joined += cells[i];
    }
    return addList(name, joined, data);
  }

  LVITEMW item{};
  item.mask = LVIF_TEXT | LVIF_PARAM;
  item.iItem = int(SendMessageW(handle, LVM_GETITEMCOUNT, 0, 0));
  item.pszText = const_cast<wchar_t *>(cells[0].c_str());
  item.lParam = LPARAM(data);
  const int row = int(SendMessageW(handle, LVM_INSERTITEMW, 0,
                                   reinterpret_cast<LPARAM>(&item)));
  if (row < 0)
    return -1;
  for (size_t column = 1; column < cells.size(); ++column) {
    LVITEMW cell{};
    cell.iSubItem = int(column);
    cell.pszText = const_cast<wchar_t *>(cells[column].c_str());
    SendMessageW(handle, LVM_SETITEMTEXTW, WPARAM(row),
                 reinterpret_cast<LPARAM>(&cell));
  }
  return row;
}

std::wstring Win32MainWindow::tableCellText(const wchar_t *name, int row,
                                            int column) const {
  const HWND handle = control(name);
  if (!handle || row < 0 || column < 0)
    return {};
  if (!isClass(handle, WC_LISTVIEW)) {
    if (column != 0)
      return {};
    wchar_t buffer[512] = {};
    SendMessageW(handle, LB_GETTEXT, WPARAM(row),
                 reinterpret_cast<LPARAM>(buffer));
    return buffer;
  }
  wchar_t buffer[512] = {};
  LVITEMW item{};
  item.iSubItem = column;
  item.pszText = buffer;
  item.cchTextMax = 511;
  SendMessageW(handle, LVM_GETITEMTEXTW, WPARAM(row),
               reinterpret_cast<LPARAM>(&item));
  return buffer;
}

int Win32MainWindow::tableColumnCount(const wchar_t *name) const {
  const HWND handle = control(name);
  if (!handle || !isClass(handle, WC_LISTVIEW))
    return 0;
  const HWND header = reinterpret_cast<HWND>(
      SendMessageW(handle, LVM_GETHEADER, 0, 0));
  return header ? int(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0)) : 0;
}

bool Win32MainWindow::isListView(const wchar_t *name) const {
  return isClass(control(name), WC_LISTVIEW);
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

HBRUSH Win32MainWindow::onCtlColor(UINT message, HDC dc, HWND child) {
  const auto found = byId_.find(GetDlgCtrlID(child));
  const WinStyleDesc *style =
      found == byId_.end() ? nullptr : styleOf(found->second->name);
  if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
    SetBkColor(dc, RGB(255, 255, 255));
    SetTextColor(dc, RGB(0, 0, 0));
    return GetSysColorBrush(COLOR_WINDOW);
  }
  if (style && style->back != kTransparent) {
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, style->back);
    SetTextColor(dc, style->text);
    const auto brush = brushes_.find(style->back);
    if (brush != brushes_.end())
      return brush->second;
    const HBRUSH created = CreateSolidBrush(style->back);
    brushes_[style->back] = created;
    return created;
  }
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
  return faceBrush_;
}

void Win32MainWindow::toggleControl(int index) {
  auto session = memory_;
  if (!session)
    return;
  const std::wstring name = L"checkBox_" + std::to_wstring(index + 1);
  const bool on = checked(name.c_str());
  if (index < 0 || index >= 7)
    return;
  if (index == 6) {
    LegacyAutoReturnPatch::set(session, profile_, on);
    return;
  }
  std::vector<ResolvedPatch> patches;
  try {
    RuntimeModuleImage image(session->imagePath, session);
    patches = ProfileRegistry().resolvePatches(image, kPatchKeys[index]);
  } catch (const std::exception &error) {
    setStatus(L"当前画像没有该控制项的配方：" + wide(error.what()));
    setChecked(name.c_str(), false);
    return;
  }
  if (patches.empty()) {
    setStatus(L"当前画像没有该控制项的签名配方");
    setChecked(name.c_str(), false);
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
  if (pages_[0])
    EnableWindow(pages_[0], TRUE);
  for (int i = 0; i < 8; ++i)
    if (toolButtons_[i])
      EnableWindow(toolButtons_[i], TRUE);
  if (business_) {
    business_->populateCatalogs();
    business_->updateEffectLayout();
    business_->refreshCurrentPage(currentPage_);
  }
  return true;
}

void Win32MainWindow::beginWindowSelection() {
  selecting_ = true;
  hoverWindow_ = nullptr;
  SetCapture(hwnd_);
  SetCursor(LoadCursorW(instance_, MAKEINTRESOURCEW(102)));
  setStatus(L"拖动到目标游戏窗口后松开左键，右键或 Esc 取消");
}

void Win32MainWindow::hoverWindowSelection() {
  POINT point{};
  GetCursorPos(&point);
  HWND target = WindowFromPoint(point);
  if (target)
    target = GetAncestor(target, GA_ROOT);
  if (target == hwnd_)
    target = nullptr;
  if (target == hoverWindow_)
    return;
  hoverWindow_ = target;
  if (target) {
    wchar_t title[256] = {};
    GetWindowTextW(target, title, 256);
    setText(L"label_window1", title);
  } else {
    setText(L"label_window1", L"");
  }
}

void Win32MainWindow::finishWindowSelection(bool attach) {
  selecting_ = false;
  ReleaseCapture();
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
  if (!attach || !hoverWindow_)
    return;
  DWORD pid = 0;
  GetWindowThreadProcessId(hoverWindow_, &pid);
  if (!pid)
    return;
  try {
    attachPid(pid, false);
  } catch (const std::exception &error) {
    setStatus(wide(error.what()));
  }
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
  if (notifyHandler_)
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
  report["success"] = memory_ != nullptr;
  if (!memory_)
    report["error"] = "尚未附加引擎";
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

LRESULT CALLBACK Win32MainWindow::containerProc(HWND hwnd, UINT message,
                                                WPARAM wparam, LPARAM lparam,
                                                UINT_PTR, DWORD_PTR ref) {
  auto *self = reinterpret_cast<Win32MainWindow *>(ref);
  if (!self)
    return DefSubclassProc(hwnd, message, wparam, lparam);
  if (hwnd == self->frame_) {
    if (message == WM_ERASEBKGND)
      return 1;
    if (message == WM_PAINT) {
      PAINTSTRUCT paint{};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT client{};
      GetClientRect(hwnd, &client);
      FillRect(dc, &client, self->faceBrush_);
      DrawEdge(dc, &client, EDGE_RAISED, BF_RECT);
      EndPaint(hwnd, &paint);
      return 0;
    }
  }
  switch (message) {
  case WM_COMMAND:
  case WM_NOTIFY:
  case WM_CONTEXTMENU:
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLORLISTBOX:
  case WM_DRAWITEM:
  case WM_PARENTNOTIFY:
    if (self->hwnd_)
      return SendMessageW(self->hwnd_, message, wparam, lparam);
    break;
  default:
    break;
  }
  return DefSubclassProc(hwnd, message, wparam, lparam);
}

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
      RECT outer{0, 0, kClientWidth, kClientHeight};
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
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
      return reinterpret_cast<LRESULT>(
          self->onCtlColor(message, reinterpret_cast<HDC>(wparam),
                           reinterpret_cast<HWND>(lparam)));
    case WM_DRAWITEM:
      if (wparam >= 9000 && wparam < 9000 + kWinToolCount) {
        self->paintTool(reinterpret_cast<DRAWITEMSTRUCT *>(lparam));
        return TRUE;
      }
      break;
    case WM_PARENTNOTIFY:
      if (LOWORD(wparam) == WM_LBUTTONDOWN &&
          reinterpret_cast<HWND>(lparam) == self->control(L"label_pivot")) {
        self->beginWindowSelection();
        return 0;
      }
      break;
    case WM_MOUSEMOVE:
      if (self->selecting_) {
        self->hoverWindowSelection();
        return 0;
      }
      break;
    case WM_LBUTTONUP:
      if (self->selecting_) {
        self->finishWindowSelection(true);
        return 0;
      }
      break;
    case WM_RBUTTONDOWN:
      if (self->selecting_) {
        self->finishWindowSelection(false);
        return 0;
      }
      break;
    case WM_KEYDOWN:
      if (self->selecting_ && wparam == VK_ESCAPE) {
        self->finishWindowSelection(false);
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
    case WM_NCDESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      self->hwnd_ = nullptr;
      self->frame_ = nullptr;
      for (auto &page : self->pages_)
        page = nullptr;
      break;
    default:
      break;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

namespace {

int runWindowImpl(int argc, wchar_t **argv, bool &headless) {
  StartupOptions options;
  if (!parseOptions(argc, argv, options))
    return 2;
  const bool smokeArmed = !options.smokePath.empty();
  const bool diagnosticArmed = !options.diagnosticPath.empty();
  headless = smokeArmed || diagnosticArmed;

  INITCOMMONCONTROLSEX commonControls{};
  commonControls.dwSize = sizeof(commonControls);
  commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&commonControls);

  Win32MainWindow window(GetModuleHandleW(nullptr), diagnosticArmed);
  if (!window.create()) {
    const std::wstring reason = window.startupError();
    log("create failed: " + utf8(reason));
    if (!headless)
      MessageBoxW(nullptr,
                 (L"窗口创建失败：" + reason +
                  L"\n\n日志：%LOCALAPPDATA%\\CaoCaoToolbox\\logs\\native.log")
                     .c_str(),
                 L"扳手启动失败", MB_OK | MB_ICONERROR);
    return 5;
  }
  ShowWindow(window.handle(), SW_SHOW);
  UpdateWindow(window.handle());

  if (smokeArmed) {
    SetTimer(window.handle(), kSmokeTimer, 250, nullptr);
  } else if (options.attachPid) {
    try {
      window.attachPid(*options.attachPid, diagnosticArmed);
    } catch (const std::exception &error) {
      window.setStatus(wide(error.what()));
    }
    if (diagnosticArmed)
      SetTimer(window.handle(), kDiagnosticTimer, 3000, nullptr);
  }

  bool loggedStrayTimer = false;
  MSG message{};
  while (true) {
    const BOOL got = GetMessageW(&message, nullptr, 0, 0);
    if (got == 0)
      break;
    if (got == -1) {
      log("GetMessageW failed");
      return 3;
    }
    // A stray WM_TIMER used to be mistaken for the smoke timer and killed the
    // window.  Keep one diagnostic line in native.log in case it reappears.
    if (!headless && !loggedStrayTimer && message.message == WM_TIMER) {
      log("stray WM_TIMER id=" + std::to_string(message.wParam));
      loggedStrayTimer = true;
    }
    if (smokeArmed && message.hwnd == window.handle() &&
        message.message == WM_TIMER && message.wParam == kSmokeTimer) {
      KillTimer(window.handle(), kSmokeTimer);
      const bool ok =
          IsWindow(window.handle()) && IsWindowVisible(window.handle());
      Json report;
      report["schema"] = "banshou-startup-smoke-v1";
      report["success"] = ok;
      report["gameMemoryWrites"] = false;
      report["remoteCalls"] = false;
      report["windowCreated"] = ok;
      report["qtPlatform"] = "windows";
      report["pages"] = Json::array();
      for (int page = 1; page <= 8; ++page)
        report["pages"].push_back("widget_" + std::to_string(page));
      bool written = true;
      try {
        writeText(options.smokePath, report.dump(2) + "\n");
      } catch (const std::exception &error) {
        written = false;
        log(std::string("smoke report failed: ") + error.what());
      }
      if (options.smokeExit)
        PostQuitMessage(ok && written ? 0 : 5);
      continue;
    }
    if (diagnosticArmed && message.hwnd == window.handle() &&
        message.message == WM_TIMER && message.wParam == kDiagnosticTimer) {
      KillTimer(window.handle(), kDiagnosticTimer);
      std::string body;
      int code = 2;
      try {
        const Json report = Json::parse(window.diagnosticSnapshot());
        code = report.value("success", false) ? 0 : 2;
        body = report.dump(2) + "\n";
      } catch (const std::exception &error) {
        log(std::string("diagnostic snapshot failed: ") + error.what());
      }
      bool written = true;
      try {
        if (!body.empty())
          writeText(options.diagnosticPath, body);
      } catch (const std::exception &error) {
        written = false;
        log(std::string("diagnostic report failed: ") + error.what());
      }
      if (options.diagnosticExit)
        PostQuitMessage(written ? code : 5);
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return static_cast<int>(message.wParam);
}

}  // namespace

int runWindow(int argc, wchar_t **argv) {
  bool headless = false;
  try {
    return runWindowImpl(argc, argv, headless);
  } catch (const std::exception &error) {
    log(std::string("fatal: ") + error.what());
    if (!headless)
      MessageBoxW(nullptr,
                  (L"启动失败：" + wide(error.what()) +
                   L"\n\n日志：%LOCALAPPDATA%\\CaoCaoToolbox\\logs\\native.log")
                      .c_str(),
                  L"扳手启动失败", MB_OK | MB_ICONERROR);
    return 10;
  } catch (...) {
    log("fatal: unknown exception");
    if (!headless)
      MessageBoxW(nullptr, L"启动失败：未知异常", L"扳手启动失败",
                  MB_OK | MB_ICONERROR);
    return 11;
  }
}

}  // namespace banshou
