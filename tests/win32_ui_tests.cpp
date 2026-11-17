#include "win32_mainwindow.hpp"
#include "platform.hpp"

#include "win32_control_table.inc"

#include <commctrl.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace banshou;

namespace {

int failures = 0;

void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << std::endl;
    ++failures;
  }
}

struct Child {
  HWND handle = nullptr;
  HWND parent = nullptr;
  int id = 0;
  std::wstring className;
  std::wstring text;
};

BOOL CALLBACK collect(HWND window, LPARAM parameter) {
  auto *children = reinterpret_cast<std::vector<Child> *>(parameter);
  wchar_t buffer[512] = {};
  GetClassNameW(window, buffer, 511);
  Child child;
  child.handle = window;
  child.parent = GetParent(window);
  child.id = GetDlgCtrlID(window);
  child.className = buffer;
  buffer[0] = 0;
  GetWindowTextW(window, buffer, 511);
  child.text = buffer;
  children->push_back(child);
  return TRUE;
}

const Child *byId(const std::vector<Child> &children, int id) {
  for (const auto &child : children)
    if (child.id == id)
      return &child;
  return nullptr;
}

const WinControlDesc *descOf(const wchar_t *name) {
  for (const auto &entry : kControls)
    if (wcscmp(entry.name, name) == 0)
      return &entry;
  return nullptr;
}

const Child *byName(const std::vector<Child> &children, const wchar_t *name) {
  const WinControlDesc *desc = descOf(name);
  return desc ? byId(children, desc->id) : nullptr;
}

RECT absoluteRect(HWND hwnd, HWND root) {
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  POINT origin{rect.left, rect.top};
  MapWindowPoints(nullptr, root, &origin, 1);
  OffsetRect(&rect, origin.x - rect.left, origin.y - rect.top);
  return rect;
}

RECT expectedRect(const WinControlDesc &desc, int toolbarHeight) {
  RECT rect{desc.x, desc.y, desc.x + desc.w, desc.y + desc.h};
  if (desc.page >= 0 && desc.page <= 8) {
    OffsetRect(&rect, 9, 77 + toolbarHeight);
  } else {
    OffsetRect(&rect, 0, toolbarHeight);
  }
  return rect;
}

int columnCount(HWND list) {
  const HWND header =
      reinterpret_cast<HWND>(SendMessageW(list, LVM_GETHEADER, 0, 0));
  return header ? int(SendMessageW(header, HDM_GETITEMCOUNT, 0, 0)) : 0;
}

int columnWidth(HWND list, int column) {
  return int(SendMessageW(list, LVM_GETCOLUMNWIDTH, WPARAM(column), 0));
}

}  // namespace

int main() {
  INITCOMMONCONTROLSEX controls{};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&controls);

  Win32MainWindow window(GetModuleHandleW(nullptr));
  if (!window.create()) {
    std::cerr << "create failed: " << utf8(window.startupError()) << std::endl;
    return 1;
  }
  const HWND root = window.handle();
  ShowWindow(root, SW_SHOW);
  UpdateWindow(root);
  const int toolbarHeight = window.toolbarHeight();
  check(toolbarHeight > 0 && toolbarHeight < 60, "toolbar height is sane");

  RECT client{};
  GetClientRect(root, &client);
  check(client.right == 700 && client.bottom == 530,
        "client area must be 700x530");
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(root, GWL_STYLE));
  check(!(style & WS_THICKFRAME), "window must not be resizable");
  check(!(style & WS_MAXIMIZEBOX), "window must not be maximizable");
  check(GetClassLongPtrW(root, GCLP_HBRBACKGROUND) != 0,
        "window class needs a background brush");

  std::vector<Child> children;
  EnumChildWindows(root, collect, reinterpret_cast<LPARAM>(&children));
  check(children.size() >= 324, "native controls are missing");

  // The former white mask: label must live inside the hidden widget_0 page.
  const Child *label = byName(children, L"label");
  check(label != nullptr, "label control is missing");
  if (label) {
    check(label->parent == window.pagePanel(8),
          "label must be parented to the hidden widget_0 page");
    check(!IsWindowVisible(label->handle), "label must not be visible");
  }

  // Page panels: exactly one visible page at a time.
  for (int page = 0; page < 8; ++page) {
    window.showPage(page);
    check(window.currentPage() == page, "page switch failed");
    for (int other = 0; other < 8; ++other) {
      const bool visible = IsWindowVisible(window.pagePanel(other)) != FALSE;
      check(visible == (other == page), "page panel visibility is wrong");
    }
    check(!IsWindowVisible(window.pagePanel(8)),
          "the hidden page must never be shown");
  }
  window.showPage(0);

  // Geometry: page controls are offset by the frame, central controls by the
  // toolbar; both must match the generated table exactly.
  const struct RectCase {
    const wchar_t *name;
  } rectCases[] = {{L"checkBox_1"},     {L"listview_diy"}, {L"war_life"},
                   {L"war_input_51"},  {L"war_input_54"}, {L"var_save_3"},
                   {L"label_title"},   {L"label_pivot"},  {L"label_help"},
                   {L"version_1"}};
  for (const auto &test : rectCases) {
    const WinControlDesc *desc = descOf(test.name);
    check(desc != nullptr, "geometry probe is missing from the table");
    const Child *child = byName(children, test.name);
    check(child != nullptr, "geometry probe control is missing");
    if (!desc || !child)
      continue;
    const RECT actual = absoluteRect(child->handle, root);
    const RECT expected = expectedRect(*desc, toolbarHeight);
    check(EqualRect(&actual, &expected), "control geometry differs");
  }

  // Toolbar buttons must stay inside the toolbar strip.
  for (int i = 0; i < 8; ++i) {
    const int id = 9000 + i;
    const Child *child = byId(children, id);
    check(child != nullptr, "toolbar button is missing");
    if (!child)
      continue;
    const RECT rect = absoluteRect(child->handle, root);
    check(rect.bottom <= toolbarHeight, "toolbar button overlaps the content");
  }
  for (const wchar_t *name : {L"label_title", L"label_help", L"label_pivot"}) {
    const Child *child = byName(children, name);
    check(child != nullptr, "toolbar overlap probe is missing");
    if (!child)
      continue;
    const RECT left = absoluteRect(child->handle, root);
    for (int i = 0; i < 8; ++i) {
      const Child *tool = byId(children, 9000 + i);
      if (!tool)
        continue;
      RECT intersection{};
      const RECT right = absoluteRect(tool->handle, root);
      check(!IntersectRect(&intersection, &left, &right),
            "toolbar button overlaps a header control");
    }
  }

  // Texts that were fixed in the previous round must stay fixed.
  const Child *control = byName(children, L"checkBox_1");
  check(control && control->text == L"可控制敌军和友军",
        "baseline control text differs");
  const Child *revive = byName(children, L"checkBox_9");
  check(revive && revive->text == L"我军自动复活",
        "checkbox 9 must be labelled as auto revive");
  for (const auto &child : children)
    check(child.text != L"省略攻击动作",
          "stale omit-attack-action label is still present");
  const Child *pivot = byName(children, L"label_pivot");
  check(pivot != nullptr, "pivot control is missing");
  if (pivot)
    check(SendMessageW(pivot->handle, STM_GETIMAGE, IMAGE_BITMAP, 0) != 0,
          "pivot bitmap resource is missing");
  const Child *merit = byName(children, L"item_7");
  check(merit && merit->text.find(L"功勋池") != std::wstring::npos,
        "merit pool label differs");
  const Child *kill = byName(children, L"war_kill");
  check(kill && kill->text == L"区域灭敌", "area kill label differs");

  // Tables must have real columns, otherwise they render as empty boxes.
  const struct TableCase {
    const wchar_t *name;
    int columns;
    int widths[6];
  } tables[] = {{L"listview_var_1", 3, {60, 55, 95, 0, 0, 0}},
                {L"listview_var_2", 3, {45, 75, 90, 0, 0, 0}},
                {L"listview_var_3", 3, {45, 75, 90, 0, 0, 0}},
                {L"listview_diy", 6, {100, 80, 90, 70, 45, 230}}};
  for (const auto &test : tables) {
    const Child *child = byName(children, test.name);
    check(child != nullptr, "table control is missing");
    if (!child)
      continue;
    check(columnCount(child->handle) == test.columns,
          "table column count differs");
    for (int column = 0; column < test.columns; ++column)
      check(columnWidth(child->handle, column) == test.widths[column],
            "table column width differs");
  }

  // Row data: lParam is the only source of truth, cells must round-trip.
  {
    const HWND list = window.control(L"listview_var_1");
    window.clearControl(L"listview_var_1");
    window.addRow(L"listview_var_1", {L"7", L"True", L"0x401000"}, 7);
    check(window.listCount(L"listview_var_1") == 1, "addRow did not append");
    check(window.listData(L"listview_var_1", 0) == 7, "row lParam was lost");
    check(window.tableCellText(L"listview_var_1", 0, 1) == L"True",
          "cell text was lost");
    check(window.listSelection(L"listview_var_1") == -1,
          "fresh table must not have a selection");
    window.clearControl(L"listview_var_1");
    check(window.listCount(L"listview_var_1") == 0,
          "clearControl must empty a list view");
  }

  // List boxes use a different message family.
  {
    window.addList(L"listview_data", L"1：test", 1);
    check(window.listCount(L"listview_data") == 1, "list box append failed");
    window.clearControl(L"listview_data");
    check(window.listCount(L"listview_data") == 0,
          "clearControl must empty a list box");
  }

  // Class consistency: the message family must match the real control class.
  for (const auto &entry : kControls) {
    const Child *child = byId(children, entry.id);
    if (!child)
      continue;
    if (entry.kind == CTL_TABLE)
      check(child->className == WC_LISTVIEW,
            "CTL_TABLE must create a list view");
    else if (entry.kind == CTL_LIST)
      check(child->className == L"ListBox", "CTL_LIST must create a list box");
    else if (entry.kind == CTL_COMBO)
      check(child->className == L"ComboBox", "CTL_COMBO must create a combo box");
  }

  // Notification chain: a click inside a page panel must reach the business
  // layer (which reports "no person selected" without a session).
  {
    const Child *save = byName(children, L"data_save");
    check(save != nullptr, "person save button is missing");
    if (save) {
      window.setText(L"label_window2", L"");
      SendMessageW(root, WM_COMMAND,
                   MAKEWPARAM(WORD(save->id), BN_CLICKED),
                   reinterpret_cast<LPARAM>(save->handle));
      const std::wstring status = window.text(L"label_window2");
      check(!status.empty(), "page button click never reached the window");
    }
    const Child *box = byName(children, L"checkBox_1");
    check(box != nullptr, "control checkbox is missing");
    if (box) {
      SendMessageW(box->handle, BM_CLICK, 0, 0);
      SendMessageW(box->handle, BM_CLICK, 0, 0);
      check(true, "checkbox click must not crash without a session");
    }
  }

  // Initial state must match the original designer defaults.
  check(window.checked(L"version_1"), "version_1 (auto) must start checked");
  check(window.checked(L"war_kind_1"), "war_kind_1 (ours) must start checked");
  check(window.text(L"var_input_1") == L"0", "var_input_1 must start at 0");
  check(window.text(L"war_input_49") == L"255", "war_input_49 must start at 255");
  for (int i = 0; i < 8; ++i)
    check(!IsWindowEnabled(window.toolButton(i)),
          "toolbar must stay disabled before attaching");
  check(!IsWindowEnabled(window.pagePanel(0)),
        "control page must stay disabled before attaching");

  // Text must fit: buttons and labels use different paddings.
  HDC dc = GetDC(root);
  for (const auto &entry : kControls) {
    if (entry.kind != CTL_BUTTON && entry.kind != CTL_CHECK &&
        entry.kind != CTL_RADIO && entry.kind != CTL_LABEL)
      continue;
    const Child *child = byId(children, entry.id);
    if (!child || child->text.empty())
      continue;
    const HFONT font = reinterpret_cast<HFONT>(
        SendMessageW(child->handle, WM_GETFONT, 0, 0));
    HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SIZE extent{};
    GetTextExtentPoint32W(dc, child->text.c_str(), int(child->text.size()),
                          &extent);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    if (previous)
      SelectObject(dc, previous);
    const int padding = entry.kind == CTL_LABEL
                            ? 2
                            : (entry.kind == CTL_BUTTON ? 14 : 19);
    if (extent.cx + padding > entry.w || metrics.tmHeight - 2 > entry.h) {
      char message[256] = {};
      std::snprintf(message, sizeof(message),
                    "text fit %ls: w=%d need=%d h=%d textH=%d", entry.name,
                    entry.w, int(extent.cx) + padding, entry.h,
                    int(metrics.tmHeight));
      check(false, message);
    }
  }
  ReleaseDC(root, dc);

  ShowWindow(root, SW_HIDE);
  if (failures) {
    std::cerr << failures << " checks failed" << std::endl;
    return 1;
  }
  std::cout << "win32 ui contracts passed" << std::endl;
  return 0;
}
