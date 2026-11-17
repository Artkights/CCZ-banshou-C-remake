#include "win32_mainwindow.hpp"
#include "platform.hpp"

#include <commctrl.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "win32_control_table.inc"

using namespace banshou;

static void require(bool value, const char *message) {
  if (!value) {
    std::cerr << message << std::endl;
    std::exit(1);
  }
}

namespace {
struct Child {
  HWND handle;
  int id;
  std::wstring className;
  std::wstring text;
  RECT rect;
};

BOOL CALLBACK collectChildren(HWND hwnd, LPARAM lparam) {
  auto *children = reinterpret_cast<std::vector<Child> *>(lparam);
  wchar_t buffer[512] = {};
  GetClassNameW(hwnd, buffer, sizeof(buffer) / sizeof(buffer[0]));
  const std::wstring className = buffer;
  buffer[0] = 0;
  GetWindowTextW(hwnd, buffer, sizeof(buffer) / sizeof(buffer[0]));
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  HWND parent = GetParent(hwnd);
  POINT origin{rect.left, rect.top};
  MapWindowPoints(nullptr, parent, &origin, 1);
  OffsetRect(&rect, origin.x - rect.left, origin.y - rect.top);
  children->push_back({hwnd, GetDlgCtrlID(hwnd), className, buffer, rect});
  return TRUE;
}

// Absolute position relative to the main window client area.
RECT absoluteRect(HWND hwnd, HWND root) {
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  POINT origin{rect.left, rect.top};
  MapWindowPoints(nullptr, root, &origin, 1);
  OffsetRect(&rect, origin.x - rect.left, origin.y - rect.top);
  return rect;
}

const Child *findChild(const std::vector<Child> &children, const wchar_t *name) {
  // Geometry and text are matched by the generated control id; object names
  // are not stored in Win32, so tests look up by id via the descriptor table.
  for (const auto &entry : kControls) {
    if (wcscmp(entry.name, name) != 0)
      continue;
    for (const auto &child : children)
      if (child.id == entry.id)
        return &child;
  }
  return nullptr;
}
}  // namespace

int main() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  INITCOMMONCONTROLSEX commonControls{};
  commonControls.dwSize = sizeof(commonControls);
  commonControls.dwICC = ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&commonControls);

  Win32MainWindow window(instance);
  require(window.create(), "native window creation failed");
  const HWND root = window.handle();

  RECT client{};
  GetClientRect(root, &client);
  require(client.right - client.left == 700 && client.bottom - client.top == 530,
          "client area is not 700x530");
  const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(root, GWL_STYLE));
  require(!(style & WS_THICKFRAME) && !(style & WS_MAXIMIZEBOX),
          "main window is resizable");

  std::vector<Child> children;
  EnumChildWindows(root, collectChildren,
                   reinterpret_cast<LPARAM>(&children));
  require(children.size() >= 319, "native controls are missing");
  for (int id = 9000; id < 9008; ++id) {
    const bool found = std::any_of(
        children.begin(), children.end(),
        [id](const Child &child) { return child.id == id; });
    require(found, "toolbar button is missing");
  }

  // All of these controls live inside a page panel, whose origin is the
  // Designer frame position (9,77) inside the 700x530 client area.
  constexpr int offsetX = 9, offsetY = 77;
  const struct RectCase {
    const wchar_t *name;
    RECT expected;  // raw .ui geometry
  } rectCases[] = {
      {L"checkBox_1", {20, 38, 191, 57}},
      {L"listview_diy", {10, 50, 671, 381}},
      {L"war_life", {550, 300, 671, 335}},
      {L"war_input_51", {185, 170, 210, 195}},
      {L"war_input_54", {255, 200, 280, 225}},
      {L"var_save_3", {610, 20, 660, 45}},
  };
  for (const auto &test : rectCases) {
    const Child *child = findChild(children, test.name);
    require(child, "a contract control is missing");
    const RECT absolute = absoluteRect(child->handle, root);
    RECT expected = test.expected;
    OffsetRect(&expected, offsetX, offsetY);
    require(EqualRect(&absolute, &expected), "control geometry differs");
  }

  const Child *control = findChild(children, L"checkBox_1");
  require(control && control->text == L"可控制敌军和友军",
          "baseline control text differs");
  const Child *revive = findChild(children, L"checkBox_9");
  require(revive && revive->text == L"我军自动复活",
          "checkbox 9 must be labelled as auto revive");
  for (const auto &child : children)
    require(child.text != L"省略攻击动作",
            "stale omit-attack-action label is still present");

  const struct TextCase {
    const wchar_t *name;
    const wchar_t *text;
  } textCases[] = {
      {L"item_7", L"功勋池"}, {L"war_kill", L"区域灭敌"},
      {L"var_save_1", L"保存"},
  };
  for (const auto &test : textCases) {
    const Child *child = findChild(children, test.name);
    require(child && child->text.find(test.text) != std::wstring::npos,
            "a control text differs from the baseline");
  }

  const struct OverlapCase {
    const wchar_t *left;
    const wchar_t *right;
  } overlaps[] = {
      {L"var_input_4", L"var_save_1"},
      {L"mk_1_11", L"mk_input_1_11"},
      {L"war_input_28", L"war_30"},
  };
  for (const auto &test : overlaps) {
    const Child *left = findChild(children, test.left);
    const Child *right = findChild(children, test.right);
    require(left && right, "overlap probe control is missing");
    const RECT a = absoluteRect(left->handle, root);
    const RECT b = absoluteRect(right->handle, root);
    RECT intersection{};
    require(!IntersectRect(&intersection, &a, &b),
            "two controls still overlap each other");
  }

  for (int index = 51; index <= 54; ++index) {
    const std::wstring name = L"war_input_" + std::to_wstring(index);
    const Child *child = findChild(children, name.c_str());
    require(child && !IsWindowVisible(child->handle),
            "extended condition inputs must start hidden");
  }

  for (int page = 0; page < 8; ++page) {
    window.showPage(page);
    require(window.currentPage() == page, "page switch failed");
  }

  std::cout << "win32 ui contracts passed" << std::endl;
  return 0;
}
