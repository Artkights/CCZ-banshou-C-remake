#include "window.hpp"
#include "diy.hpp"
#include "remote.hpp"
#include <algorithm>
#include <array>
#include <commctrl.h>
#include <commdlg.h>
#include <memory>
#include <sstream>
#include <tuple>
#include <vector>

namespace banshou {
namespace {
enum : int {
  IDC_ATTACH = 1001,
  IDC_DETACH,
  IDC_STATUS,
  IDC_TABS,
  IDC_LIST,
  IDC_VERSION = 1010,
  IDC_SAVE = 1200,
  IDC_REFRESH,
  IDC_DIY_OPEN,
  IDC_DIY_SAVE,
  IDC_LIFE,
  IDC_KILL,
  IDC_LOCK,
  IDC_UNLOCK,
  IDC_TURN,
  IDC_RECALC,
  IDC_CONNECT,
  IDC_DIY_WRITE = 1517,
  IDC_DIY_PREV = 1518,
  IDC_GROUP_APPLY = 1519,
  IDC_CONTROL_BASE = 1100,
  IDC_EDIT_BASE = 1300,
  IDC_LIFE_DIR = 1500,
  IDC_WEATHER,
  IDC_FACTION,
  IDC_GROUP_BASE = 1520,
  IDC_STRATEGY = 1540,
  IDC_STRATEGY_TARGET,
  IDC_CONSUMABLE_LIST = 1560,
  IDC_CONSUMABLE_VALUE,
  IDC_ALL_TREASURE,
  IDC_ALL_CONSUMABLE,
  IDC_CLEAR_ITEMS,
  IDC_VAR_LIST_BASE = 1600,
  IDC_VAR_PREV_BASE = 1610,
  IDC_VAR_NEXT_BASE = 1620,
  IDC_VAR_SAVE_BASE = 1630
};
constexpr UINT_PTR kLockTimer = 1, kAutoLifeTimer = 2;
const wchar_t *const kPages[] = {L"控制", L"人物", L"战场", L"仓库",
                                 L"天赋", L"变量", L"DIY",  L"必杀"};
const wchar_t *const kControlNames[] = {
    L"可控制敌军和友军", L"不分敌我",         L"禁止AI行动",
    L"待命后可移动",     L"移动力 = 255",     L"穿越移动",
    L"自动回归",         L"托管我军主动出击", L"我军自动复活"};
const char *const kPatchKeys[] = {
    "control_friendly",        "control_all_factions", "control_no_ai",
    "control_move_after_wait", "control_move_255",     "control_pass_through",
    "control_auto_return"};
const wchar_t *const kPersonLabels[] = {
    L"编号",     L"R形象",    L"S形象",  L"头像",     L"HP上限",   L"MP上限",
    L"攻击",     L"防御",     L"精神",   L"爆发",     L"士气",     L"等级",
    L"经验",     L"兵种",     L"武器",   L"武器等级", L"武器经验", L"防具",
    L"防具等级", L"防具经验", L"辅助",   L"字段28",   L"字段06",   L"字段44",
    L"字段46",   L"杀敌数",   L"武功勋", L"统功勋",   L"智功勋",   L"敏功勋",
    L"运功勋",   L"我军"};
const wchar_t *const kBattleLabels[] = {
    L"槽位",   L"角色",   L"HP",     L"MP",       L"SP",       L"X",
    L"Y",      L"行动",   L"AI",     L"方向",     L"健康",     L"攻击",
    L"防御",   L"精神",   L"爆发",   L"士气",     L"移动",     L"目标X",
    L"目标Y",  L"字段22", L"字段23", L"字段24",   L"字段26",   L"字段28",
    L"字段2A", L"字段2C", L"字段2E", L"回合上限", L"当前回合", L"下一关",
    L"天气",   L"季节",   L"可见",   L"阵营",     L"目标槽",   L"显示槽",
    L"复活X",  L"复活Y",  L"X1",     L"Y1",       L"X2",       L"Y2",
    L"方针X",  L"方针Y"};
const wchar_t *const kWarehouseLabels[] = {
    L"槽位", L"物品ID", L"等级", L"经验", L"金钱", L"忠奸度", L"功勋池"};
const wchar_t *const kPersonExtraLabels[] = {L"武力", L"统率", L"智力", L"敏捷",
                                             L"运气", L"出场", L"撤退"};
const wchar_t *const kEffectLabels[] = {
    L"编号",  L"人物1", L"值1",   L"人物2", L"值2",   L"人物3", L"值3",
    L"人物4", L"值4",   L"兵种1", L"值1",   L"兵种2", L"值2"};
const wchar_t *const kFatalLabels[] = {L"编号",  L"人物1", L"等级1", L"人物2",
                                       L"等级2", L"人物3", L"等级3", L"人物4",
                                       L"等级4", L"人物5", L"等级5", L"效果值"};
const wchar_t *const kDirections[] = {L"——", L"上", L"右", L"下", L"左"};
const wchar_t *const kWeather[] = {L"晴", L"阴", L"小雨", L"大雨", L"雪"};
const wchar_t *const kFactions[] = {L"我", L"友", L"敌"};
const wchar_t *const kGroupBuffs[] = {L"攻击", L"防御", L"精神",    L"爆发",
                                      L"士气", L"移动", L"全部状态"};
const wchar_t *const kGroupValues[] = {L"不变", L"弱", L"中", L"强"};
const wchar_t *const kStrategies[] = {L"被动出击",  L"主动出击", L"坚守原地",
                                      L"攻击武将",  L"到指定点", L"跟随武将",
                                      L"逃到指定点"};
const wchar_t *const kVariableTitles[] = {L"布尔变量", L"整数变量",
                                          L"指针变量"};
} // namespace

struct LockedUnit {
  size_t slot = 0;
  uint16_t dataId = 0xffff;
  uint32_t hp = 0, mp = 0;
};
static std::wstring numberText(uint64_t value) {
  return std::to_wstring(value);
}

class App {
public:
  HWND hwnd{}, tabs{}, list{}, status{}, versionChoice{}, save{}, refresh{},
      diyOpen{}, diySave{}, life{}, kill{}, lock{}, unlock{}, turn{}, recalc{},
      connect{};
  std::array<HWND, 9> controls{};
  std::array<bool, 9> controlState{};
  std::array<HWND, 80> edits{};
  std::array<HWND, 80> editLabels{};
  std::array<HWND, 7> personExtraEdits{}, personExtraLabels{};
  HWND lifeDir{}, weather{}, faction{}, groupBuff{}, groupValue{}, groupApply{},
      battleStrategy{}, battleTarget{}, diySection{}, diyValue{}, diyWrite{},
      diyPrev{}, diyNext{}, consumableList{}, consumableValue{}, allTreasure{},
      allConsumable{}, clearItems{};
  std::array<HWND, 3> variableLists{}, variableLabels{}, variableStarts{},
      variableValues{}, variablePrev{}, variableNext{}, variableSave{};
  std::array<size_t, 3> variableOffsets{};
  SessionController session;
  bool selecting = false, autoLife = false;
  uint64_t uiGeneration = 0;
  DiyCsv diy;
  std::wstring diyPath;
  size_t diySectionIndex = 0, diyOffset = 0;
  std::vector<DiyExpandedRow> diyVisible;
  std::vector<LockedUnit> locked;
  std::shared_ptr<MemorySession> remoteSession;
  std::unique_ptr<RemoteServices> remoteServices;
  UINT_PTR lockTimer = 0, autoLifeTimer = 0;

  App() {
    session.onState = [this](SessionState s, uint64_t generation,
                             const std::string &error) {
      auto *e = new std::tuple<SessionState, uint64_t, std::string>(
          s, generation, error);
      if (!hwnd ||
          !PostMessageW(hwnd, WM_APP + 1, 0, reinterpret_cast<LPARAM>(e)))
        delete e;
    };
  }
  HWND button(const wchar_t *text, int id, int x, int y, int w, int h) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, w, h, hwnd, reinterpret_cast<HMENU>(id), nullptr,
                         nullptr);
  }
  void addEdit(int index, const wchar_t *label, int x, int y) {
    editLabels[index] = CreateWindowW(L"STATIC", label, WS_CHILD, x, y, 45, 21,
                                      hwnd, nullptr, nullptr, nullptr);
    edits[index] = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        x + 46, y, 55, 22, hwnd, reinterpret_cast<HMENU>(IDC_EDIT_BASE + index),
        nullptr, nullptr);
  }
  void create(HWND h) {
    hwnd = h;
    button(L"选择游戏窗口", IDC_ATTACH, 12, 12, 130, 28);
    button(L"解绑", IDC_DETACH, 150, 12, 80, 28);
    CreateWindowW(L"STATIC", L"基础画像", WS_CHILD | WS_VISIBLE, 245, 18, 65,
                  22, hwnd, nullptr, nullptr, nullptr);
    versionChoice = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 310, 13,
        150, 220, hwnd, reinterpret_cast<HMENU>(IDC_VERSION), nullptr, nullptr);
    const wchar_t *versions[] = {L"自动识别", L"6.4", L"6.3 MP", L"6.3",
                                 L"6.2",      L"6.1", L"6.5",    L"6.6"};
    const int versionValues[] = {-1, 0, 1, 2, 3, 4, 5, 6};
    for (size_t i = 0; i < std::size(versions); ++i) {
      int item = int(SendMessageW(versionChoice, CB_ADDSTRING, 0,
                                  reinterpret_cast<LPARAM>(versions[i])));
      SendMessageW(versionChoice, CB_SETITEMDATA, item, versionValues[i]);
    }
    SendMessageW(versionChoice, CB_SETCURSEL, 0, 0);
    tabs = CreateWindowW(
        WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 12, 50,
        660, 34, hwnd, reinterpret_cast<HMENU>(IDC_TABS), nullptr, nullptr);
    for (const auto *p : kPages) {
      TCITEMW i{};
      i.mask = TCIF_TEXT;
      i.pszText = const_cast<wchar_t *>(p);
      TabCtrl_InsertItem(tabs, TabCtrl_GetItemCount(tabs), &i);
    }
    for (int i = 0; i < 9; ++i)
      controls[i] = CreateWindowW(
          L"BUTTON", kControlNames[i], WS_CHILD | BS_AUTOCHECKBOX, 24,
          95 + i * 32, 190, 25, hwnd,
          reinterpret_cast<HMENU>(IDC_CONTROL_BASE + i), nullptr, nullptr);
    list = CreateWindowW(
        L"LISTBOX", L"请先选择游戏窗口",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 12, 90,
        133, 365, hwnd, reinterpret_cast<HMENU>(IDC_LIST), nullptr, nullptr);
    for (int i = 0; i < 32; ++i)
      addEdit(i, kPersonLabels[i], 150 + (i % 5) * 104, 90 + (i / 5) * 27);
    for (int i = 0; i < 7; ++i) {
      int x = 150 + (i % 5) * 104, y = 279 + (i / 5) * 27;
      personExtraLabels[i] =
          CreateWindowW(L"STATIC", kPersonExtraLabels[i], WS_CHILD, x, y, 45,
                        21, hwnd, nullptr, nullptr, nullptr);
      personExtraEdits[i] = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
          x + 46, y, 55, 22, hwnd, nullptr, nullptr, nullptr);
    }
    for (int i = 0; i < 44; ++i)
      addEdit(32 + i, kBattleLabels[i], 150 + (i % 5) * 104, 90 + (i / 5) * 27);
    battleStrategy = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 150, 333, 120, 200, hwnd,
        reinterpret_cast<HMENU>(IDC_STRATEGY), nullptr, nullptr);
    for (const auto *s : kStrategies)
      SendMessageW(battleStrategy, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(s));
    SendMessageW(battleStrategy, CB_SETCURSEL, 0, 0);
    battleTarget = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 275, 333,
        130, 240, hwnd, reinterpret_cast<HMENU>(IDC_STRATEGY_TARGET), nullptr,
        nullptr);
    lifeDir = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 410, 333, 120, 200, hwnd,
        reinterpret_cast<HMENU>(IDC_LIFE_DIR), nullptr, nullptr);
    for (const auto *s : kDirections)
      SendMessageW(lifeDir, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(lifeDir, CB_SETCURSEL, 0, 0);
    weather = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 535, 333, 125, 160, hwnd,
        reinterpret_cast<HMENU>(IDC_WEATHER), nullptr, nullptr);
    for (const auto *s : kWeather)
      SendMessageW(weather, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(weather, CB_SETCURSEL, 0, 0);
    faction = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 150, 363, 90, 120, hwnd,
        reinterpret_cast<HMENU>(IDC_FACTION), nullptr, nullptr);
    for (const auto *s : kFactions)
      SendMessageW(faction, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(faction, CB_SETCURSEL, 0, 0);
    groupBuff = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 245, 363, 90, 180, hwnd,
        reinterpret_cast<HMENU>(IDC_GROUP_BASE), nullptr, nullptr);
    for (const auto *s : kGroupBuffs)
      SendMessageW(groupBuff, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(groupBuff, CB_SETCURSEL, 0, 0);
    groupValue = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 340, 363, 70, 120, hwnd,
        reinterpret_cast<HMENU>(IDC_GROUP_BASE + 1), nullptr, nullptr);
    for (const auto *s : kGroupValues)
      SendMessageW(groupValue, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
    SendMessageW(groupValue, CB_SETCURSEL, 0, 0);
    groupApply = button(L"批量状态", IDC_GROUP_APPLY, 415, 363, 90, 25);
    diySection = CreateWindowW(
        L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST, 150, 333, 180, 22, hwnd,
        reinterpret_cast<HMENU>(IDC_GROUP_BASE + 2), nullptr, nullptr);
    diyValue = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        335, 333, 110, 22, hwnd, reinterpret_cast<HMENU>(IDC_GROUP_BASE + 3),
        nullptr, nullptr);
    diyWrite = button(L"写入当前值", IDC_DIY_WRITE, 450, 331, 100, 26);
    diyPrev = button(L"上一页", IDC_DIY_PREV, 150, 400, 70, 26);
    diyNext = button(L"下一页", IDC_GROUP_BASE + 4, 225, 400, 70, 26);
    save = button(L"保存当前", IDC_SAVE, 540, 435, 120, 28);
    refresh = button(L"刷新", IDC_REFRESH, 440, 435, 95, 28);
    diyOpen = button(L"打开 DIY.csv", IDC_DIY_OPEN, 300, 400, 110, 26);
    diySave = button(L"保存 DIY", IDC_DIY_SAVE, 415, 400, 100, 26);
    recalc = button(L"能力重新计算", IDC_RECALC, 540, 333, 120, 28);
    connect = button(L"连接至人物", IDC_CONNECT, 510, 363, 150, 25);
    life = button(L"战场复活", IDC_LIFE, 150, 400, 90, 28);
    kill = button(L"区域灭敌", IDC_KILL, 245, 400, 90, 28);
    lock = button(L"增加锁定", IDC_LOCK, 340, 400, 90, 28);
    unlock = button(L"移除锁定", IDC_UNLOCK, 435, 400, 90, 28);
    turn = button(L"转向", IDC_TURN, 530, 400, 130, 28);
    consumableList = CreateWindowW(
        L"LISTBOX", L"", WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 150,
        170, 300, 220, hwnd, reinterpret_cast<HMENU>(IDC_CONSUMABLE_LIST),
        nullptr, nullptr);
    CreateWindowW(L"STATIC", L"消耗品数量", WS_CHILD, 455, 170, 90, 22, hwnd,
                  reinterpret_cast<HMENU>(IDC_CONSUMABLE_VALUE + 100), nullptr,
                  nullptr);
    consumableValue = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"0", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        550, 168, 85, 24, hwnd, reinterpret_cast<HMENU>(IDC_CONSUMABLE_VALUE),
        nullptr, nullptr);
    allTreasure = button(L"一键全宝", IDC_ALL_TREASURE, 150, 400, 100, 28);
    allConsumable = button(L"全消耗品", IDC_ALL_CONSUMABLE, 255, 400, 100, 28);
    clearItems = button(L"清空仓库", IDC_CLEAR_ITEMS, 360, 400, 100, 28);
    for (int kind = 0; kind < 3; ++kind) {
      int x = 12 + kind * 216;
      variableLabels[kind] =
          CreateWindowW(L"STATIC", kVariableTitles[kind], WS_CHILD, x, 92, 205,
                        22, hwnd, nullptr, nullptr, nullptr);
      variableLists[kind] = CreateWindowW(
          L"LISTBOX", L"", WS_CHILD | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, x,
          116, 205, 274, hwnd,
          reinterpret_cast<HMENU>(IDC_VAR_LIST_BASE + kind), nullptr, nullptr);
      variableStarts[kind] =
          CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"0",
                          WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, x, 396, 50, 24,
                          hwnd, nullptr, nullptr, nullptr);
      variablePrev[kind] =
          button(L"上页", IDC_VAR_PREV_BASE + kind, x + 55, 396, 55, 24);
      variableNext[kind] =
          button(L"下页", IDC_VAR_NEXT_BASE + kind, x + 115, 396, 55, 24);
      variableValues[kind] = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
          x, 426, 100, 24, hwnd, nullptr, nullptr, nullptr);
      variableSave[kind] =
          button(L"写入", IDC_VAR_SAVE_BASE + kind, x + 105, 426, 65, 24);
    }
    status = CreateWindowW(L"STATIC", L"未附加", WS_CHILD | WS_VISIBLE, 12, 468,
                           660, 24, hwnd, reinterpret_cast<HMENU>(IDC_STATUS),
                           nullptr, nullptr);
    setPageVisibility();
  }
  int page() const { return TabCtrl_GetCurSel(tabs); }
  void show(HWND h, bool value) { ShowWindow(h, value ? SW_SHOW : SW_HIDE); }
  void setSharedLabels(const wchar_t *const *labels, size_t count) {
    for (size_t i = 0; i < 32; ++i)
      SetWindowTextW(editLabels[i], i < count ? labels[i] : L"");
  }
  void setPageVisibility() {
    int p = page();
    bool cp = p == 0, bp = p == 2, wp = p == 3, vp = p == 5, dp = p == 6;
    size_t sharedCount = 0;
    if (p == 1) {
      setSharedLabels(kPersonLabels, std::size(kPersonLabels));
      sharedCount = std::size(kPersonLabels);
    } else if (p == 3) {
      setSharedLabels(kWarehouseLabels, std::size(kWarehouseLabels));
      sharedCount = std::size(kWarehouseLabels);
    } else if (p == 4) {
      setSharedLabels(kEffectLabels, std::size(kEffectLabels));
      sharedCount = std::size(kEffectLabels);
    } else if (p == 7) {
      setSharedLabels(kFatalLabels, std::size(kFatalLabels));
      sharedCount = std::size(kFatalLabels);
    }
    for (auto h : controls)
      show(h, cp);
    show(list, !cp && !vp);
    for (size_t i = 0; i < edits.size(); ++i) {
      bool v = bp ? (i >= 32 && i < 76) : (i < sharedCount);
      show(editLabels[i], v);
      show(edits[i], v);
    }
    show(save, p == 1 || p == 2 || p == 3 || p == 4 || p == 7);
    show(refresh, true);
    show(diyOpen, dp);
    show(diySave, dp);
    show(diySection, dp);
    show(diyValue, dp);
    show(diyWrite, dp);
    show(diyPrev, dp);
    show(diyNext, dp);
    show(recalc, p == 1);
    show(connect, bp);
    show(life, bp);
    show(kill, bp);
    show(lock, bp);
    show(unlock, bp);
    show(turn, bp);
    show(battleStrategy, bp);
    show(battleTarget, bp);
    show(lifeDir, bp);
    show(weather, bp);
    show(faction, bp);
    show(groupBuff, bp);
    show(groupValue, bp);
    show(groupApply, bp);
    show(consumableList, wp);
    show(consumableValue, wp);
    show(GetDlgItem(hwnd, IDC_CONSUMABLE_VALUE + 100), wp);
    show(allTreasure, wp);
    show(allConsumable, wp);
    show(clearItems, wp);
    for (int kind = 0; kind < 3; ++kind) {
      show(variableLabels[kind], vp);
      show(variableLists[kind], vp);
      show(variableStarts[kind], vp);
      show(variableValues[kind], vp);
      show(variablePrev[kind], vp);
      show(variableNext[kind], vp);
      show(variableSave[kind], vp);
    }
    for (int i = 0; i < 7; ++i) {
      show(personExtraLabels[i], p == 1);
      show(personExtraEdits[i], p == 1);
    }
  }
  void setEdit(int i, uint64_t v) {
    if (i >= 0 && i < (int)edits.size())
      SetWindowTextW(edits[i], numberText(v).c_str());
  }
  uint32_t getEdit(int i, uint32_t max = UINT32_MAX) const {
    wchar_t t[64]{};
    GetWindowTextW(edits[i], t, 64);
    if (!t[0])
      throw Error("字段不能为空");
    std::wstring s(t);
    size_t pos = 0;
    auto v = std::stoull(s, &pos, 0);
    if (pos != s.size() || v > max)
      throw Error("字段数值越界");
    return (uint32_t)v;
  }
  void clearEdits() {
    for (auto h : edits)
      SetWindowTextW(h, L"");
  }
  void setStatus(const std::wstring &s) { SetWindowTextW(status, s.c_str()); }
  void setStatus(const std::string &s) { setStatus(wide(s)); }
  size_t selectedSlot() const {
    int row = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (row < 0)
      return SIZE_MAX;
    auto v = SendMessageW(list, LB_GETITEMDATA, row, 0);
    return v == LB_ERR ? size_t(row) : size_t(v);
  }
  void choose() {
    if (selecting)
      return;
    selecting = true;
    SetFocus(hwnd);
    SetCapture(hwnd);
    setStatus(L"请单击目标游戏窗口；按 Esc 取消");
  }
  void finishSelect() {
    if (!selecting)
      return;
    ReleaseCapture();
    selecting = false;
    POINT pt{};
    GetCursorPos(&pt);
    HWND target = GetAncestor(WindowFromPoint(pt), GA_ROOT);
    if (!target || target == hwnd || IsChild(hwnd, target)) {
      setStatus(L"未选择有效的外部窗口");
      return;
    }
    try {
      int selected = int(SendMessageW(versionChoice, CB_GETCURSEL, 0, 0));
      int versionHint =
          selected < 0
              ? -1
              : int(SendMessageW(versionChoice, CB_GETITEMDATA, selected, 0));
      auto gen = session.attach(target, versionHint);
      uiGeneration = std::max(uiGeneration, gen);
    } catch (const std::exception &e) {
      setStatus(std::string("附加失败：") + e.what());
    }
  }
  void stopTimers() {
    if (lockTimer) {
      KillTimer(hwnd, kLockTimer);
      lockTimer = 0;
    }
    if (autoLifeTimer) {
      KillTimer(hwnd, kAutoLifeTimer);
      autoLifeTimer = 0;
    }
    autoLife = false;
    locked.clear();
    remoteServices.reset();
    remoteSession.reset();
  }
  RemoteServices &remoteFor(const std::shared_ptr<MemorySession> &s,
                            const EngineProfile &p) {
    if (!remoteServices || remoteSession.get() != s.get()) {
      remoteServices = std::make_unique<RemoteServices>(s, p);
      remoteSession = s;
    }
    return *remoteServices;
  }

  uint32_t parseUnsigned(HWND edit, uint32_t maximum, const char *field) const {
    wchar_t text[64]{};
    GetWindowTextW(edit, text, 64);
    if (!text[0])
      throw Error(std::string(field) + "不能为空");
    std::wstring value(text);
    size_t consumed = 0;
    auto parsed = std::stoull(value, &consumed, 0);
    if (consumed != value.size() || parsed > maximum)
      throw Error(std::string(field) + "数值越界");
    return uint32_t(parsed);
  }
  int32_t parseSigned(HWND edit, const char *field) const {
    wchar_t text[64]{};
    GetWindowTextW(edit, text, 64);
    if (!text[0])
      throw Error(std::string(field) + "不能为空");
    std::wstring value(text);
    size_t consumed = 0;
    auto parsed = std::stoll(value, &consumed, 0);
    if (consumed != value.size() || parsed < INT32_MIN || parsed > INT32_MAX)
      throw Error(std::string(field) + "数值越界");
    return int32_t(parsed);
  }
  std::wstring gameText(const std::shared_ptr<MemorySession> &s,
                        uint32_t address, size_t width) const {
    auto bytes = s->read(address, width);
    auto end = std::find(bytes.begin(), bytes.end(), uint8_t(0));
    if (end == bytes.begin())
      return L"";
    int count = int(end - bytes.begin());
    for (UINT codePage : {UINT(54936), UINT(936)}) {
      auto needed = MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                        reinterpret_cast<LPCCH>(bytes.data()),
                                        count, nullptr, 0);
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
  std::wstring itemName(const std::shared_ptr<MemorySession> &s,
                        const EngineProfile &p, uint16_t id) const {
    auto table = p.address("item_table", 0);
    if (!table)
      return L"物品" + std::to_wstring(id);
    try {
      return gameText(s,
                      checkedAddress(table, uint64_t(id) * 25,
                                     p.limit("item_name_size", 12)),
                      p.limit("item_name_size", 12));
    } catch (...) {
      return L"物品" + std::to_wstring(id);
    }
  }
  void refreshConsumables(const std::shared_ptr<MemorySession> &s,
                          const EngineProfile &p) {
    SendMessageW(consumableList, LB_RESETCONTENT, 0, 0);
    WarehouseService service(s, p);
    for (size_t i = 0; i < service.consumableCount(); ++i) {
      auto id = service.consumableItemId(i);
      std::wstringstream line;
      line << id << L":" << itemName(s, p, id) << L"  数量="
           << int(service.readConsumable(i));
      auto value = line.str();
      int row = int(SendMessageW(consumableList, LB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(value.c_str())));
      SendMessageW(consumableList, LB_SETITEMDATA, row, LPARAM(i));
    }
    if (service.consumableCount()) {
      SendMessageW(consumableList, LB_SETCURSEL, 0, 0);
      SetWindowTextW(consumableValue,
                     numberText(service.readConsumable(0)).c_str());
    }
  }
  void loadConsumable() {
    auto s = session.session();
    if (!s)
      return;
    int row = int(SendMessageW(consumableList, LB_GETCURSEL, 0, 0));
    if (row < 0)
      return;
    auto index = size_t(SendMessageW(consumableList, LB_GETITEMDATA, row, 0));
    auto value = WarehouseService(s, session.profile()).readConsumable(index);
    SetWindowTextW(consumableValue, numberText(value).c_str());
  }
  void refreshVariables() {
    auto s = session.session();
    if (!s)
      return;
    VariableService service(s, session.profile());
    for (int kind = 0; kind < 3; ++kind) {
      SendMessageW(variableLists[kind], LB_RESETCONTENT, 0, 0);
      auto offset = std::min<size_t>(variableOffsets[kind], 4095);
      variableOffsets[kind] = offset;
      SetWindowTextW(variableStarts[kind], numberText(offset).c_str());
      auto cells = service.readPage(VariableKind(kind), offset,
                                    std::min<size_t>(50, 4096 - offset));
      for (const auto &cell : cells) {
        std::wstringstream line;
        line << cell.index << L"  0x" << std::hex << std::uppercase
             << cell.address << L"  " << std::dec;
        if (kind == 0)
          line << (cell.value ? L"True" : L"False");
        else if (kind == 1)
          line << int32_t(cell.value);
        else
          line << L"0x" << std::hex << cell.value;
        auto value = line.str();
        int row = int(SendMessageW(variableLists[kind], LB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(value.c_str())));
        SendMessageW(variableLists[kind], LB_SETITEMDATA, row,
                     LPARAM(cell.index));
      }
      if (!cells.empty()) {
        SendMessageW(variableLists[kind], LB_SETCURSEL, 0, 0);
        auto value = kind == 0   ? uint32_t(service.readBool(offset))
                     : kind == 1 ? uint32_t(service.readInt(offset))
                                 : service.readPointer(offset);
        SetWindowTextW(
            variableValues[kind],
            (kind == 1 ? std::to_wstring(int32_t(value)) : numberText(value))
                .c_str());
      }
    }
  }
  void loadVariable(int kind) {
    auto s = session.session();
    if (!s || kind < 0 || kind >= 3)
      return;
    int row = int(SendMessageW(variableLists[kind], LB_GETCURSEL, 0, 0));
    if (row < 0)
      return;
    auto index =
        size_t(SendMessageW(variableLists[kind], LB_GETITEMDATA, row, 0));
    VariableService service(s, session.profile());
    if (kind == 0)
      SetWindowTextW(variableValues[kind],
                     service.readBool(index) ? L"1" : L"0");
    else if (kind == 1)
      SetWindowTextW(variableValues[kind],
                     std::to_wstring(service.readInt(index)).c_str());
    else
      SetWindowTextW(variableValues[kind],
                     numberText(service.readPointer(index)).c_str());
  }
  void saveVariable(int kind) {
    auto s = session.session();
    if (!s || kind < 0 || kind >= 3)
      throw Error("尚未附加游戏");
    int row = int(SendMessageW(variableLists[kind], LB_GETCURSEL, 0, 0));
    if (row < 0)
      throw Error("未选择变量");
    auto index =
        size_t(SendMessageW(variableLists[kind], LB_GETITEMDATA, row, 0));
    VariableService service(s, session.profile());
    if (kind == 0)
      service.writeBool(
          index, parseUnsigned(variableValues[kind], 1, "布尔变量") != 0);
    else if (kind == 1)
      service.writeInt(index, parseSigned(variableValues[kind], "整数变量"));
    else
      service.writePointer(
          index, parseUnsigned(variableValues[kind], UINT32_MAX, "指针变量"));
    refreshVariables();
  }

  void refreshDiy(const std::shared_ptr<MemorySession> &s,
                  const EngineProfile &p) {
    if (diyPath.empty()) {
      SendMessageW(list, LB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(L"请打开 DIY.csv"));
      return;
    }
    SendMessageW(diySection, CB_RESETCONTENT, 0, 0);
    for (const auto &name : diy.sectionNames())
      SendMessageW(diySection, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(wide(name).c_str()));
    if (diy.sectionCount() == 0)
      return;
    if (diySectionIndex >= diy.sectionCount())
      diySectionIndex = 0;
    SendMessageW(diySection, CB_SETCURSEL, (WPARAM)diySectionIndex, 0);
    diyVisible = diy.expand(diySectionIndex, diyOffset, 200);
    if (diyVisible.empty() && diyOffset) {
      diyOffset = 0;
      diyVisible = diy.expand(diySectionIndex, 0, 200);
    }
    for (size_t i = 0; i < diyVisible.size(); ++i) {
      auto &row = diyVisible[i];
      try {
        row.value = s->u(row.address, row.width, false);
      } catch (...) {
        row.value = 0;
      }
      std::wstringstream text;
      text << wide(row.label) << L" 0x" << std::hex << row.address << L" ["
           << std::dec << int(row.width) << L"] = " << row.value;
      if (!row.kind.empty())
        text << L" (" << wide(row.kind) << L")";
      if (row.kind == "战场编号") {
        auto total = p.limit("battle_ours", 20) +
                     p.limit("battle_friends", 40) +
                     p.limit("battle_enemies", 190);
        if (row.value < total) {
          auto unit = BattleService(s, p).read(row.value);
          text << (unit.dataId == 0xffff
                       ? L" 空槽"
                       : L" 角色=" + std::to_wstring(unit.dataId));
        } else {
          text << L" 无效编号";
        }
      } else if (row.kind == "Data编号") {
        text << (row.value < p.limit("person_count", 1024)
                     ? L" 人物=" + std::to_wstring(row.value)
                     : L" 无效编号");
      } else if (row.kind == "宝物") {
        text << (row.value < p.limit("item_count", 255)
                     ? L" " + itemName(s, p, uint16_t(row.value))
                     : L" 无效编号");
      }
      if (!row.explanation.empty())
        text << L" " << wide(row.explanation);
      auto str = text.str();
      int item = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                   reinterpret_cast<LPARAM>(str.c_str()));
      SendMessageW(list, LB_SETITEMDATA, item, (LPARAM)i);
    }
    if (!diyVisible.empty()) {
      SendMessageW(list, LB_SETCURSEL, 0, 0);
      SetWindowTextW(diyValue, numberText(diyVisible[0].value).c_str());
    }
  }

  void refreshPage() {
    setPageVisibility();
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    auto s = session.session();
    if (!s) {
      if (page() != 0)
        SendMessageW(list, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"请先附加游戏进程"));
      return;
    }
    try {
      auto p = session.profile();
      if (page() == 5) {
        refreshVariables();
        return;
      }
      std::wstringstream line;
      if (page() == 0)
        return;
      if (page() == 1) {
        PersonService svc(s, p);
        for (uint32_t i = 0;
             i < std::min<uint32_t>(p.limit("person_count", 1024), 256); ++i) {
          auto r = svc.read(uint16_t(i));
          line << L"#" << i << L" HP=" << r.hp << L" MP=" << r.mp << L" 武力="
               << r.strength;
          auto text = line.str();
          int row = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(text.c_str()));
          SendMessageW(list, LB_SETITEMDATA, row, i);
          line.str(L"");
          line.clear();
        }
      } else if (page() == 2) {
        BattleService svc(s, p);
        auto ours = p.limit("battle_ours", 20);
        auto friends = p.limit("battle_friends", 40);
        auto enemies = p.limit("battle_enemies", 190);
        int selectedFaction = int(SendMessageW(faction, CB_GETCURSEL, 0, 0));
        uint32_t first = selectedFaction <= 0   ? 0
                         : selectedFaction == 1 ? ours
                                                : ours + friends;
        uint32_t last = selectedFaction <= 0   ? ours
                        : selectedFaction == 1 ? ours + friends
                                               : ours + friends + enemies;
        for (uint32_t i = first; i < std::min<uint32_t>(last, 256); ++i) {
          auto r = svc.read(i);
          line << L"槽" << i << L" 数据=" << r.dataId << L" HP=" << r.hp
               << L" MP=" << r.mp << L" 坐标=" << int(r.x) << L"," << int(r.y)
               << L" 可见=" << int(r.visibility);
          auto text = line.str();
          int row = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(text.c_str()));
          SendMessageW(list, LB_SETITEMDATA, row, i);
          line.str(L"");
          line.clear();
        }
      } else if (page() == 3) {
        WarehouseService svc(s, p);
        for (size_t i = 0; i < 200; ++i) {
          auto r = svc.readRecord(i);
          line << L"槽" << i << L" "
               << (r.item == 0xff ? L"空" : itemName(s, p, r.item)) << L" ["
               << int(r.item) << L"] 等级=" << int(r.level) << L" 经验="
               << int(r.experience);
          auto text = line.str();
          int row = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(text.c_str()));
          SendMessageW(list, LB_SETITEMDATA, row, i);
          line.str(L"");
          line.clear();
        }
        refreshConsumables(s, p);
      } else if (page() == 4) {
        EffectService svc(s, p);
        for (size_t i = 0;
             i < std::min<uint32_t>(p.limit("effect_record_count", 255), 256);
             ++i) {
          auto r = svc.read(i);
          line << L"特效" << i << L" 人物=" << r.characters[0] << L","
               << r.characters[1] << L"," << r.characters[2] << L","
               << r.characters[3];
          auto text = line.str();
          int row = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(text.c_str()));
          SendMessageW(list, LB_SETITEMDATA, row, i);
          line.str(L"");
          line.clear();
        }
      } else if (page() == 6) {
        refreshDiy(s, p);
      } else if (page() == 7) {
        FatalService svc(s, p);
        for (size_t i = 0;
             i < std::min<uint32_t>(p.limit("fatal_count", 80), 80); ++i) {
          auto r = svc.read(i);
          line << L"必杀" << i << L" 人物=" << r.persons[0] << L","
               << r.persons[1] << L"," << r.persons[2];
          auto text = line.str();
          int row = (int)SendMessageW(list, LB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(text.c_str()));
          SendMessageW(list, LB_SETITEMDATA, row, i);
          line.str(L"");
          line.clear();
        }
      }
      if (SendMessageW(list, LB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(list, LB_SETCURSEL, 0, 0);
        loadEditor();
      }
    } catch (const std::exception &e) {
      setStatus(std::string("读取失败：") + e.what());
    }
  }

  void loadEditor() {
    auto s = session.session();
    if (!s)
      return;
    size_t row = selectedSlot();
    if (row == SIZE_MAX)
      return;
    try {
      auto p = session.profile();
      clearEdits();
      if (page() == 1) {
        auto r = PersonService(s, p).read((uint16_t)row);
        setEdit(0, r.id);
        setEdit(1, r.r);
        setEdit(2, r.s);
        setEdit(3, r.face);
        setEdit(4, r.hp);
        setEdit(5, r.mp);
        setEdit(6, r.strength);
        setEdit(7, r.defense);
        setEdit(8, r.intelligence);
        setEdit(9, r.agility);
        setEdit(10, r.luck);
        setEdit(11, r.level);
        setEdit(12, r.exp);
        setEdit(13, r.job);
        setEdit(14, r.weapon);
        setEdit(15, r.weaponLevel);
        setEdit(16, r.weaponExp);
        setEdit(17, r.armor);
        setEdit(18, r.armorLevel);
        setEdit(19, r.armorExp);
        setEdit(20, r.accessory);
        setEdit(21, r.field28);
        setEdit(22, r.field06);
        setEdit(23, r.field44);
        setEdit(24, r.field46);
        setEdit(25, r.kills);
        for (int i = 0; i < 5; ++i)
          setEdit(26 + i, r.merits[i]);
        setEdit(31, r.ours);
        for (int i = 0; i < 5; ++i)
          SetWindowTextW(personExtraEdits[i],
                         numberText(r.baseStats[i]).c_str());
        SetWindowTextW(personExtraEdits[5], numberText(r.appearance).c_str());
        SetWindowTextW(personExtraEdits[6], numberText(r.retreat).c_str());
      } else if (page() == 2) {
        auto r = BattleService(s, p).read(row);
        setEdit(32, row);
        setEdit(33, r.dataId);
        setEdit(34, r.hp);
        setEdit(35, r.mp);
        setEdit(36, r.sp);
        setEdit(37, r.x);
        setEdit(38, r.y);
        setEdit(39, r.action);
        setEdit(40, r.ai);
        setEdit(41, r.direction);
        setEdit(42, r.health);
        for (int i = 0; i < 6; ++i)
          setEdit(43 + i, r.buffs[i]);
        setEdit(49, r.targetX);
        setEdit(50, r.targetY);
        setEdit(51, r.field22);
        setEdit(52, r.field23);
        for (int i = 0; i < 6; ++i)
          setEdit(53 + i, r.expanded[i]);
        auto b = BattleStateService(s, p).read();
        setEdit(59, b.turnLimit);
        setEdit(60, b.currentTurn);
        setEdit(61, b.nextScenario);
        setEdit(62, b.weather);
        setEdit(63, b.season);
        setEdit(64, r.visibility);
        setEdit(65, r.faction);
        setEdit(66, r.targetSlot);
        setEdit(67, r.displaySlot);
        setEdit(68, r.x);
        setEdit(69, r.y);
        setEdit(70, 0);
        setEdit(71, 0);
        setEdit(72, 255);
        setEdit(73, 255);
        setEdit(74, r.strategyX);
        setEdit(75, r.strategyY);
        int strategy = 0;
        if (r.ai == 1)
          strategy = 1;
        else if (r.ai == 2)
          strategy = 2;
        else if (r.ai == 0)
          strategy = r.targetSlot == 0xff ? 4 : 3;
        else if (r.ai == 4)
          strategy = r.targetSlot == 0xff ? 6 : 5;
        SendMessageW(battleStrategy, CB_SETCURSEL, strategy, 0);
        SendMessageW(battleTarget, CB_RESETCONTENT, 0, 0);
        int targetSelection = -1;
        auto total = p.limit("battle_ours", 20) +
                     p.limit("battle_friends", 40) +
                     p.limit("battle_enemies", 190);
        BattleService battle(s, p);
        for (uint32_t i = 0; i < total; ++i) {
          auto candidate = battle.read(i);
          if (candidate.dataId == 0xffff)
            continue;
          std::wstring text = L"槽" + std::to_wstring(i) + L" 角色" +
                              std::to_wstring(candidate.dataId);
          int item = int(SendMessageW(battleTarget, CB_ADDSTRING, 0,
                                      reinterpret_cast<LPARAM>(text.c_str())));
          SendMessageW(battleTarget, CB_SETITEMDATA, item, LPARAM(i));
          if (i == r.targetSlot)
            targetSelection = item;
        }
        if (targetSelection < 0 &&
            SendMessageW(battleTarget, CB_GETCOUNT, 0, 0) > 0)
          targetSelection = 0;
        SendMessageW(battleTarget, CB_SETCURSEL, targetSelection, 0);
        EnableWindow(battleTarget, strategy == 3 || strategy == 5);
        SendMessageW(weather, CB_SETCURSEL, b.weather, 0);
        SendMessageW(lifeDir, CB_SETCURSEL, 0, 0);
        EnableWindow(life,
                     (r.dataId != 0xffff && r.hp == 0 && r.visibility == 3)
                         ? TRUE
                         : FALSE);
      } else if (page() == 3) {
        auto r = WarehouseService(s, p).readRecord(row);
        setEdit(0, row);
        setEdit(1, r.item);
        setEdit(2, r.level);
        setEdit(3, r.experience);
        setEdit(4, s->u(p.address("money", 0x004B077C), 4));
        setEdit(5, s->u(p.address("alignment", 0x004B0782), 1));
        setEdit(6, s->u(p.address("merit_pool", 0x00505F40), 4));
      } else if (page() == 4) {
        auto r = EffectService(s, p).read(row);
        setEdit(0, row);
        for (int i = 0; i < 4; ++i) {
          setEdit(i * 2 + 1, r.characters[i]);
          setEdit(i * 2 + 2, r.characterValues[i]);
        }
        setEdit(9, r.jobs[0]);
        setEdit(10, r.jobValues[0]);
        setEdit(11, r.jobs[1]);
        setEdit(12, r.jobValues[1]);
      } else if (page() == 5) {
        auto v = VariableService(s, p);
        setEdit(0, row);
        setEdit(1, v.readBool(row));
        setEdit(2, (uint32_t)v.readInt(row));
        setEdit(3, v.readPointer(row));
      } else if (page() == 6) {
        if (row < diyVisible.size())
          SetWindowTextW(diyValue, numberText(diyVisible[row].value).c_str());
      } else if (page() == 7) {
        auto r = FatalService(s, p).read(row);
        setEdit(0, row);
        for (int i = 0; i < 5; ++i) {
          setEdit(i * 2 + 1, r.persons[i]);
          setEdit(i * 2 + 2, r.levels[i]);
        }
        setEdit(11, r.value);
      }
    } catch (const std::exception &e) {
      setStatus(std::string("读取失败：") + e.what());
    }
  }

  void savePage() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto p = session.profile();
    size_t row = selectedSlot();
    if (row == SIZE_MAX)
      throw Error("未选择记录");
    if (page() == 1) {
      auto r = PersonService(s, p).read((uint16_t)row);
      r.r = uint16_t(getEdit(1, 65535));
      r.s = uint16_t(getEdit(2, 65535));
      r.face = uint16_t(getEdit(3, 65535));
      r.hp = getEdit(4);
      r.mp = uint16_t(getEdit(5, 65535));
      r.strength = uint16_t(getEdit(6, 65535));
      r.defense = uint16_t(getEdit(7, 65535));
      r.intelligence = uint16_t(getEdit(8, 65535));
      r.agility = uint16_t(getEdit(9, 65535));
      r.luck = uint16_t(getEdit(10, 65535));
      r.level = uint8_t(getEdit(11, 255));
      r.exp = uint8_t(getEdit(12, 255));
      r.job = uint8_t(getEdit(13, 255));
      r.weapon = uint8_t(getEdit(14, 255));
      r.weaponLevel = uint8_t(getEdit(15, 255));
      r.weaponExp = uint8_t(getEdit(16, 255));
      r.armor = uint8_t(getEdit(17, 255));
      r.armorLevel = uint8_t(getEdit(18, 255));
      r.armorExp = uint8_t(getEdit(19, 255));
      r.accessory = uint8_t(getEdit(20, 255));
      r.field28 = uint8_t(getEdit(21, 255));
      r.field06 = uint16_t(getEdit(22, 65535));
      r.field44 = uint16_t(getEdit(23, 65535));
      r.field46 = uint16_t(getEdit(24, 65535));
      r.kills = uint16_t(getEdit(25, 65535));
      for (int i = 0; i < 5; ++i)
        r.merits[i] = uint16_t(getEdit(26 + i, 65535));
      r.ours = uint8_t(getEdit(31, 255));
      for (int i = 0; i < 5; ++i)
        r.baseStats[i] =
            uint8_t(parseUnsigned(personExtraEdits[i], 255, "人物五维"));
      r.appearance =
          uint8_t(parseUnsigned(personExtraEdits[5], 255, "出场状态"));
      r.retreat = uint8_t(parseUnsigned(personExtraEdits[6], 255, "撤退状态"));
      PersonService(s, p).write((uint16_t)row, r);
    } else if (page() == 2) {
      auto r = BattleService(s, p).read(row);
      auto oldX = r.x, oldY = r.y, oldDirection = r.direction;
      r.dataId = uint16_t(getEdit(33, 65535));
      r.hp = getEdit(34);
      r.mp = getEdit(35);
      r.sp = uint8_t(getEdit(36, 255));
      r.x = uint8_t(getEdit(37, 255));
      r.y = uint8_t(getEdit(38, 255));
      r.action = uint8_t(getEdit(39, 255));
      r.direction = uint8_t(getEdit(41, 255));
      int directionChoice = int(SendMessageW(lifeDir, CB_GETCURSEL, 0, 0));
      if (directionChoice > 0)
        r.direction = uint8_t(directionChoice - 1);
      r.health = uint8_t(getEdit(42, 255));
      for (int i = 0; i < 6; ++i)
        r.buffs[i] = uint8_t(getEdit(43 + i, 255));
      r.targetX = uint8_t(getEdit(49, 255));
      r.targetY = uint8_t(getEdit(50, 255));
      r.field22 = uint8_t(getEdit(51, 255));
      r.field23 = uint8_t(getEdit(52, 255));
      for (int i = 0; i < 6; ++i)
        r.expanded[i] = uint8_t(getEdit(53 + i, 255));
      r.visibility = uint8_t(getEdit(64, 255));
      r.faction = uint8_t(getEdit(65, 255));
      r.displaySlot = uint8_t(getEdit(67, 255));
      r.strategyX = uint8_t(getEdit(74, 255));
      r.strategyY = uint8_t(getEdit(75, 255));
      int strategy = int(SendMessageW(battleStrategy, CB_GETCURSEL, 0, 0));
      if (strategy < 0 || strategy >= 7)
        throw Error("战场方针无效");
      static const uint8_t aiValues[] = {3, 1, 2, 0, 0, 4, 4};
      r.ai = aiValues[strategy];
      if (strategy == 3 || strategy == 5) {
        int target = int(SendMessageW(battleTarget, CB_GETCURSEL, 0, 0));
        if (target < 0)
          throw Error("方针需要选择目标单位");
        r.targetSlot =
            uint8_t(SendMessageW(battleTarget, CB_GETITEMDATA, target, 0));
      } else if (strategy == 4 || strategy == 6) {
        r.targetSlot = 0xff;
      } else {
        r.targetSlot = uint8_t(getEdit(66, 255));
      }
      BattleService(s, p).write(row, r);
      if (oldX != r.x || oldY != r.y) {
        try {
          remoteFor(s, p).changePosition(r.direction, r.x, r.y, r.dataId);
        } catch (...) {
        }
      } else if (oldDirection != r.direction) {
        try {
          remoteFor(s, p).changeDirection(r.direction, r.dataId);
        } catch (...) {
        }
      }
      auto b = BattleStateService(s, p).read();
      b.turnLimit = uint8_t(getEdit(59, 255));
      b.currentTurn = uint8_t(getEdit(60, 255));
      b.nextScenario = uint8_t(getEdit(61, 128));
      int wi = (int)SendMessageW(weather, CB_GETCURSEL, 0, 0);
      b.weather = wi < 0 ? uint8_t(getEdit(62, 4)) : uint8_t(wi);
      b.season = uint8_t(getEdit(63, 255));
      BattleStateService(s, p).write(b);
      try {
        static const uint8_t encodedWeather[] = {0, 3, 4, 5, 0x16};
        remoteFor(s, p).changeWeather(encodedWeather[b.weather]);
      } catch (...) {
      }
    } else if (page() == 3) {
      WarehouseService(s, p).writeRecord(row, {(uint8_t)getEdit(1, 255),
                                               (uint8_t)getEdit(2, 255),
                                               (uint8_t)getEdit(3, 255)});
      int consumableRow = int(SendMessageW(consumableList, LB_GETCURSEL, 0, 0));
      if (consumableRow >= 0) {
        auto index = size_t(
            SendMessageW(consumableList, LB_GETITEMDATA, consumableRow, 0));
        WarehouseService(s, p).writeConsumable(
            index, uint8_t(parseUnsigned(consumableValue, 255, "消耗品数量")));
      }
      s->writeU(p.address("money", 0x004B077C), getEdit(4), 4);
      s->writeU(p.address("alignment", 0x004B0782), getEdit(5, 255), 1);
      s->writeU(p.address("merit_pool", 0x00505F40), getEdit(6), 4);
    } else if (page() == 4) {
      EffectRecord r{};
      for (int i = 0; i < 4; ++i) {
        r.characters[i] = (uint16_t)getEdit(i * 2 + 1, 65535);
        r.characterValues[i] = (uint8_t)getEdit(i * 2 + 2, 255);
      }
      r.jobs[0] = (uint8_t)getEdit(9, 255);
      r.jobValues[0] = (uint8_t)getEdit(10, 255);
      r.jobs[1] = (uint8_t)getEdit(11, 255);
      r.jobValues[1] = (uint8_t)getEdit(12, 255);
      EffectService(s, p).write(row, r);
    } else if (page() == 5) {
      auto v = VariableService(s, p);
      v.writeBool(row, getEdit(1, 1) != 0);
      v.writeInt(row, (int32_t)getEdit(2));
      v.writePointer(row, getEdit(3));
    } else if (page() == 7) {
      FatalRecord r{};
      for (int i = 0; i < 5; ++i) {
        r.persons[i] = (uint16_t)getEdit(i * 2 + 1, 65535);
        r.levels[i] = (uint8_t)getEdit(i * 2 + 2, 255);
      }
      r.value = (uint8_t)getEdit(11, 255);
      FatalService(s, p).write(row, r);
    }
    refreshPage();
  }

  void doRecalculate() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto row = selectedSlot();
    if (row == SIZE_MAX)
      throw Error("未选择人物");
    auto p = session.profile();
    auto base = s->u(p.address("person_pointer", 0x004CEA00), 4);
    remoteFor(s, p).recalculate(base + uint32_t(row * 0x48));
    setStatus(L"人物能力已重新计算");
  }
  void connectPerson() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto row = selectedSlot();
    if (row == SIZE_MAX)
      throw Error("未选择战场单位");
    auto id = BattleService(s, session.profile()).read(row).dataId;
    if (id == 0xffff)
      throw Error("当前槽位为空");
    TabCtrl_SetCurSel(tabs, 1);
    refreshPage();
    int count = (int)SendMessageW(list, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
      if (SendMessageW(list, LB_GETITEMDATA, i, 0) == id) {
        SendMessageW(list, LB_SETCURSEL, i, 0);
        loadEditor();
        break;
      }
    }
  }
  void applyGroup() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto p = session.profile();
    int factionIndex = (int)SendMessageW(faction, CB_GETCURSEL, 0, 0);
    auto first =
        uint32_t(factionIndex <= 0   ? 0
                 : factionIndex == 1 ? p.limit("battle_ours", 20)
                                     : p.limit("battle_ours", 20) +
                                           p.limit("battle_friends", 40));
    auto last = uint32_t(
        factionIndex <= 0 ? p.limit("battle_ours", 20)
        : factionIndex == 1
            ? p.limit("battle_ours", 20) + p.limit("battle_friends", 40)
            : p.limit("battle_ours", 20) + p.limit("battle_friends", 40) +
                  p.limit("battle_enemies", 190));
    int buff = (int)SendMessageW(groupBuff, CB_GETCURSEL, 0, 0);
    int value = (int)SendMessageW(groupValue, CB_GETCURSEL, 0, 0);
    if (buff < 0 || value <= 0)
      return;
    auto svc = BattleStateService(s, p);
    if (buff == 6) {
      for (int i = 0; i < 6; ++i)
        svc.applyGroupBuff(first, last, size_t(i), 2 * value - 1);
    } else
      svc.applyGroupBuff(first, last, size_t(buff), 2 * value - 1);
    SendMessageW(groupValue, CB_SETCURSEL, 0, 0);
    refreshPage();
  }
  void doLife() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto row = selectedSlot();
    if (row == SIZE_MAX)
      throw Error("未选择战场单位");
    auto p = session.profile();
    auto b = BattleService(s, p).read(row);
    int d = (int)SendMessageW(lifeDir, CB_GETCURSEL, 0, 0);
    if (d <= 0)
      d = int(b.direction);
    else
      --d;
    auto x = getEdit(68, 255), y = getEdit(69, 255);
    remoteFor(s, p).revive((uint32_t)d, x, y, b.dataId);
    EnableWindow(life, FALSE);
    refreshPage();
  }
  void doTurn() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto row = selectedSlot();
    if (row == SIZE_MAX)
      throw Error("未选择战场单位");
    auto p = session.profile();
    auto b = BattleService(s, p).read(row);
    int selection = int(SendMessageW(lifeDir, CB_GETCURSEL, 0, 0));
    if (selection <= 0)
      return;
    auto d = uint32_t(selection - 1);
    if (d == b.direction)
      return;
    try {
      remoteFor(s, p).changeDirection(d, b.dataId);
    } catch (...) {
      auto base = p.address("battle_units", 0x004A7B20),
           stride = p.limit("battle_stride", p.is66() ? 0x30 : 0x24);
      s->writeU(checkedAddress(base, uint64_t(row) * stride + 0x0f, 1), d, 1,
                false);
    }
    refreshPage();
  }
  void doKill() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto p = session.profile();
    auto x1 = getEdit(70, 255), y1 = getEdit(71, 255), x2 = getEdit(72, 255),
         y2 = getEdit(73, 255);
    if (x1 > x2 || y1 > y2)
      throw Error("区域坐标必须从小到大");
    auto begin = p.limit("battle_ours", 20) + p.limit("battle_friends", 40),
         end = begin + p.limit("battle_enemies", 190);
    BattleService svc(s, p);
    for (uint32_t i = begin; i < end; ++i) {
      auto b = svc.read(i);
      if (b.dataId != 0xffff && b.x >= x1 && b.x <= x2 && b.y >= y1 &&
          b.y <= y2) {
        b.hp = 0;
        svc.write(i, b);
      }
    }
    refreshPage();
  }
  void warehouseBulk(int action) {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto p = session.profile();
    WarehouseService service(s, p);
    if (action == 1) {
      auto level = uint8_t(getEdit(2, 255));
      auto experience = uint8_t(getEdit(3, 255));
      auto inserted = service.addAllTreasures(level, experience);
      setStatus(L"已写入 " + std::to_wstring(inserted) + L" 件宝物");
    } else if (action == 2) {
      service.setAllConsumables(0xff);
      setStatus(L"所有消耗品数量已设为 255");
    } else {
      service.clearWarehouse();
      service.clearConsumables();
      setStatus(L"仓库和消耗品已清空");
    }
    refreshPage();
  }
  void moveVariablePage(int kind, int direction) {
    if (kind < 0 || kind >= 3)
      return;
    if (direction < 0)
      variableOffsets[kind] =
          variableOffsets[kind] >= 50 ? variableOffsets[kind] - 50 : 0;
    else
      variableOffsets[kind] =
          std::min<size_t>(4046, variableOffsets[kind] + 50);
    refreshVariables();
  }
  void strategyChanged() {
    int strategy = int(SendMessageW(battleStrategy, CB_GETCURSEL, 0, 0));
    EnableWindow(battleTarget, strategy == 3 || strategy == 5);
  }
  void toggleLock(bool on) {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    auto row = selectedSlot();
    if (row == SIZE_MAX)
      throw Error("未选择战场单位");
    auto it =
        std::find_if(locked.begin(), locked.end(),
                     [row](const LockedUnit &u) { return u.slot == row; });
    if (on) {
      if (it != locked.end())
        return;
      auto b = BattleService(s, session.profile()).read(row);
      if (b.dataId == 0xffff)
        throw Error("当前槽位为空，不能锁定");
      locked.push_back({row, b.dataId, b.hp, b.mp});
      if (!lockTimer)
        lockTimer = SetTimer(hwnd, kLockTimer, 1000, nullptr);
    } else if (it != locked.end()) {
      locked.erase(it);
      if (locked.empty() && lockTimer) {
        KillTimer(hwnd, kLockTimer);
        lockTimer = 0;
      }
    }
  }
  void toggleAutoLife(bool on) {
    autoLife = on;
    if (on) {
      if (!autoLifeTimer)
        autoLifeTimer = SetTimer(hwnd, kAutoLifeTimer, 1000, nullptr);
    } else if (autoLifeTimer) {
      KillTimer(hwnd, kAutoLifeTimer);
      autoLifeTimer = 0;
    }
  }
  void timer(UINT_PTR id) {
    auto s = session.session();
    if (!s) {
      stopTimers();
      return;
    }
    try {
      auto p = session.profile();
      auto base = p.address("battle_units", 0x004A7B20),
           stride = p.limit("battle_stride", p.is66() ? 0x30 : 0x24);
      if (id == kLockTimer) {
        for (auto it = locked.begin(); it != locked.end();) {
          auto b = BattleService(s, p).read(it->slot);
          if (b.dataId != it->dataId) {
            it = locked.erase(it);
            continue;
          }
          s->writeU(checkedAddress(base, uint64_t(it->slot) * stride + 0x10, 4),
                    it->hp, 4, false);
          s->writeU(checkedAddress(base, uint64_t(it->slot) * stride + 0x14, 4),
                    it->mp, 4, false);
          ++it;
        }
        if (locked.empty() && lockTimer) {
          KillTimer(hwnd, kLockTimer);
          lockTimer = 0;
        }
      } else if (id == kAutoLifeTimer && autoLife) {
        auto count = p.limit("battle_ours", 20);
        for (uint32_t i = 0; i < count; ++i) {
          auto b = BattleService(s, p).read(i);
          if (b.dataId != 0xffff && b.hp == 0 && b.visibility == 3) {
            try {
              remoteFor(s, p).revive(b.direction, b.x, b.y, b.dataId);
            } catch (const std::exception &e) {
              toggleAutoLife(false);
              SendMessageW(controls[8], BM_SETCHECK, BST_UNCHECKED, 0);
              setStatus(std::string("自动复活已停止：") + e.what());
              break;
            }
          }
        }
      }
    } catch (const std::exception &e) {
      if (!s->alive())
        stopTimers();
      setStatus(std::string("定时操作失败：") + e.what());
    }
  }
  std::vector<ResolvedPatch> fixedControlPatches(const EngineProfile &p,
                                                 int index) const {
    (void)p;
    std::vector<ResolvedPatch> out;
    auto add = [&](uint32_t a, Bytes off, Bytes on) {
      out.push_back({a, std::move(off), std::move(on)});
    };
    switch (index) {
    case 0:
      add(0x0043D574, {0x74}, {0xEB});
      add(0x0043D5A5, {0x74}, {0xEB});
      add(0x0043E359, {0x74}, {0xEB});
      add(0x0044E748, {0x75, 0xC8}, {0x90, 0x90});
      add(0x0044E754, {0x75, 0xBC}, {0x90, 0x90});
      add(0x00455379, {0x74}, {0xEB});
      break;
    case 1:
      add(0x004242AF, {0x74}, {0x90});
      add(0x004242B0, {0xDD}, {0x90});
      add(0x00437BC0, {0x74, 0x34}, {0x90, 0x90});
      add(0x00454405, {0x75, 0x27}, {0x90, 0x90});
      add(0x00455646, {0x75}, {0xEB});
      break;
    case 2:
      add(0x0044E9D3, {0x74}, {0xEB});
      break;
    case 3:
      add(0x0043D5DC, {0x74}, {0xEB});
      break;
    case 4:
      add(0x0043F74F, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08},
          {0x33, 0xC0, 0x48, 0xC3, 0x90, 0x90});
      break;
    case 5:
      add(0x004387B7, {0x74}, {0xEB});
      add(0x00437D59, {0x55, 0x8B, 0xEC}, {0x33, 0xC0, 0xC3});
      break;
    default:
      break;
    }
    return out;
  }
  void toggleControl(int index) {
    if (index == 7) {
      auto s = session.session();
      if (!s)
        return;
      auto p = session.profile();
      auto base = p.address("battle_units", 0x004A7B20),
           stride = p.limit("battle_stride", 0x30),
           begin = p.limit("battle_ours", 20),
           end = begin + p.limit("battle_friends", 40);
      bool checked =
          SendMessageW(controls[index], BM_GETCHECK, 0, 0) == BST_CHECKED;
      for (uint32_t i = begin; i < end; ++i) {
        auto id = s->u(base + i * stride, 2);
        if (id != 0xffff)
          s->writeU(base + uint32_t(i * stride + 0x0e), checked ? 1 : 7, 1);
      }
      return;
    }
    if (index == 8) {
      if (!session.session()) {
        SendMessageW(controls[index], BM_SETCHECK, BST_UNCHECKED, 0);
        return;
      }
      toggleAutoLife(SendMessageW(controls[index], BM_GETCHECK, 0, 0) ==
                     BST_CHECKED);
      return;
    }
    auto s = session.session();
    if (!s)
      return;
    if (index == 6) {
      bool checked =
          SendMessageW(controls[index], BM_GETCHECK, 0, 0) == BST_CHECKED;
      try {
        LegacyAutoReturnPatch::set(s, session.profile(), checked);
        controlState[index] = checked;
      } catch (const std::exception &e) {
        SendMessageW(controls[index], BM_SETCHECK,
                     controlState[index] ? BST_CHECKED : BST_UNCHECKED, 0);
        setStatus(std::string("自动回归补丁失败：") + e.what());
      }
      return;
    }
    try {
      auto p = session.profile();
      std::vector<ResolvedPatch> patches;
      try {
        auto pi = session.process();
        RuntimeModuleImage image(pi.path, s);
        ProfileRegistry registry;
        patches = registry.resolvePatches(image, kPatchKeys[index]);
      } catch (...) {
        patches = fixedControlPatches(p, index);
      }
      if (patches.empty())
        throw Error("当前画像没有该控制项的旧版地址配方");
      PatchManager manager(s);
      bool checked =
          SendMessageW(controls[index], BM_GETCHECK, 0, 0) == BST_CHECKED;
      for (auto &site : patches) {
        auto a = s->actual(site.address);
        if (checked)
          manager.apply(a, site.original, site.replacement);
        else
          manager.restore(a, site.original);
      }
      controlState[index] = checked;
    } catch (const std::exception &e) {
      SendMessageW(controls[index], BM_SETCHECK,
                   controlState[index] ? BST_CHECKED : BST_UNCHECKED, 0);
      setStatus(std::string("补丁失败：") + e.what());
    }
  }
  void openDiy() {
    OPENFILENAMEW ofn{sizeof(ofn)};
    std::array<wchar_t, 32768> file{};
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"DIY.csv\0*.csv\0CSV\0*.csv\0\0";
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = DWORD(file.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
      diy.load(file.data());
      diyPath = file.data();
      diySectionIndex = 0;
      diyOffset = 0;
      refreshPage();
    }
  }
  void saveDiyValue() {
    auto s = session.session();
    if (!s)
      throw Error("尚未附加游戏");
    size_t row = selectedSlot();
    if (row == SIZE_MAX || row >= diyVisible.size())
      throw Error("未选择 DIY 字段");
    wchar_t text[64]{};
    GetWindowTextW(diyValue, text, 64);
    std::wstring value(text);
    size_t consumed = 0;
    auto parsed = std::stoull(value, &consumed, 0);
    if (consumed != value.size() ||
        parsed > (uint64_t(1) << (8 * diyVisible[row].width)) - 1)
      throw Error("DIY 数值超出字段宽度");
    DiyService(s).write(diyVisible[row].address, (uint32_t)parsed,
                        diyVisible[row].width);
    diyVisible[row].value = (uint32_t)parsed;
    refreshPage();
    setStatus(L"DIY 当前值已写入");
  }
  void saveDiy() {
    if (diyPath.empty())
      throw Error("尚未打开 DIY.csv");
    diy.save(diyPath);
    setStatus(L"DIY.csv 已保存（UTF-8 BOM）");
  }
  void command(WPARAM w) {
    int id = LOWORD(w);
    if (id == IDC_ATTACH)
      choose();
    else if (id == IDC_DETACH) {
      stopTimers();
      session.detach();
    } else if (id == IDC_SAVE) {
      try {
        savePage();
      } catch (const std::exception &e) {
        setStatus(std::string("保存失败：") + e.what());
      }
    } else if (id == IDC_REFRESH) {
      if (page() == 5) {
        try {
          for (int kind = 0; kind < 3; ++kind)
            variableOffsets[kind] =
                parseUnsigned(variableStarts[kind], 4095, "变量起始索引");
        } catch (const std::exception &e) {
          setStatus(std::string("变量分页失败：") + e.what());
          return;
        }
      }
      refreshPage();
    } else if (id == IDC_DIY_OPEN) {
      try {
        openDiy();
      } catch (const std::exception &e) {
        setStatus(std::string("DIY 失败：") + e.what());
      }
    } else if (id == IDC_DIY_SAVE) {
      try {
        saveDiy();
      } catch (const std::exception &e) {
        setStatus(std::string("DIY 保存失败：") + e.what());
      }
    } else if (id == IDC_DIY_WRITE) {
      try {
        saveDiyValue();
      } catch (const std::exception &e) {
        setStatus(std::string("DIY 写入失败：") + e.what());
      }
    } else if (id == IDC_DIY_PREV) {
      if (diyOffset >= 200)
        diyOffset -= 200;
      refreshPage();
    } else if (id == IDC_GROUP_BASE + 4) {
      diyOffset += 200;
      refreshPage();
    } else if (id == IDC_GROUP_BASE + 2 && HIWORD(w) == CBN_SELCHANGE) {
      diySectionIndex = (size_t)SendMessageW(diySection, CB_GETCURSEL, 0, 0);
      diyOffset = 0;
      refreshPage();
    } else if (id == IDC_RECALC) {
      try {
        doRecalculate();
      } catch (const std::exception &e) {
        setStatus(std::string("重算失败：") + e.what());
      }
    } else if (id == IDC_CONNECT) {
      try {
        connectPerson();
      } catch (const std::exception &e) {
        setStatus(std::string("连接失败：") + e.what());
      }
    } else if (id == IDC_GROUP_APPLY) {
      try {
        applyGroup();
      } catch (const std::exception &e) {
        setStatus(std::string("批量状态失败：") + e.what());
      }
    } else if (id == IDC_LIFE) {
      try {
        doLife();
      } catch (const std::exception &e) {
        setStatus(std::string("复活失败：") + e.what());
      }
    } else if (id == IDC_KILL) {
      try {
        doKill();
      } catch (const std::exception &e) {
        setStatus(std::string("击杀失败：") + e.what());
      }
    } else if (id == IDC_LOCK) {
      try {
        toggleLock(true);
      } catch (const std::exception &e) {
        setStatus(std::string("锁定失败：") + e.what());
      }
    } else if (id == IDC_UNLOCK) {
      try {
        toggleLock(false);
      } catch (const std::exception &e) {
        setStatus(std::string("解除失败：") + e.what());
      }
    } else if (id == IDC_TURN) {
      try {
        doTurn();
      } catch (const std::exception &e) {
        setStatus(std::string("转向失败：") + e.what());
      }
    } else if (id == IDC_STRATEGY && HIWORD(w) == CBN_SELCHANGE) {
      strategyChanged();
    } else if (id == IDC_FACTION && HIWORD(w) == CBN_SELCHANGE) {
      if (page() == 2)
        refreshPage();
    } else if (id == IDC_ALL_TREASURE || id == IDC_ALL_CONSUMABLE ||
               id == IDC_CLEAR_ITEMS) {
      try {
        warehouseBulk(id == IDC_ALL_TREASURE     ? 1
                      : id == IDC_ALL_CONSUMABLE ? 2
                                                 : 3);
      } catch (const std::exception &e) {
        setStatus(std::string("仓库批量操作失败：") + e.what());
      }
    } else if (id >= IDC_VAR_PREV_BASE && id < IDC_VAR_PREV_BASE + 3) {
      moveVariablePage(id - IDC_VAR_PREV_BASE, -1);
    } else if (id >= IDC_VAR_NEXT_BASE && id < IDC_VAR_NEXT_BASE + 3) {
      moveVariablePage(id - IDC_VAR_NEXT_BASE, 1);
    } else if (id >= IDC_VAR_SAVE_BASE && id < IDC_VAR_SAVE_BASE + 3) {
      try {
        saveVariable(id - IDC_VAR_SAVE_BASE);
      } catch (const std::exception &e) {
        setStatus(std::string("变量写入失败：") + e.what());
      }
    } else if (id >= IDC_CONTROL_BASE && id < IDC_CONTROL_BASE + 9)
      toggleControl(id - IDC_CONTROL_BASE);
  }
};

static App *g_app = nullptr;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    g_app = new App;
    g_app->create(h);
    return TRUE;
  }
  if (!g_app)
    return DefWindowProcW(h, m, w, l);
  if (m == WM_TIMER) {
    g_app->timer(w);
    return 0;
  }
  if (m == WM_KEYDOWN && w == VK_ESCAPE && g_app->selecting) {
    ReleaseCapture();
    g_app->selecting = false;
    g_app->setStatus(L"未附加");
    return 0;
  }
  if (m == WM_LBUTTONUP && g_app->selecting) {
    g_app->finishSelect();
    return 0;
  }
  if (m == WM_APP + 1) {
    std::unique_ptr<std::tuple<SessionState, uint64_t, std::string>> e(
        reinterpret_cast<std::tuple<SessionState, uint64_t, std::string> *>(l));
    auto state = std::get<0>(*e);
    auto generation = std::get<1>(*e);
    if (generation < g_app->uiGeneration)
      return 0;
    g_app->uiGeneration = generation;
    if (state == SessionState::Detached || state == SessionState::Error)
      g_app->stopTimers();
    g_app->setStatus(state == SessionState::Attached    ? L"已附加"
                     : state == SessionState::Attaching ? L"附加中"
                     : state == SessionState::Detaching ? L"解绑中"
                     : state == SessionState::Error     ? wide(std::get<2>(*e))
                                                        : L"未附加");
    EnableWindow(GetDlgItem(h, IDC_DETACH), state == SessionState::Attached ||
                                                state == SessionState::Error);
    if (state == SessionState::Attached)
      g_app->refreshPage();
    return 0;
  }
  if (m == WM_NOTIFY && reinterpret_cast<LPNMHDR>(l)->idFrom == IDC_TABS &&
      reinterpret_cast<LPNMHDR>(l)->code == TCN_SELCHANGE) {
    g_app->refreshPage();
    return 0;
  }
  if (m == WM_COMMAND) {
    if (HIWORD(w) == LBN_SELCHANGE) {
      int id = LOWORD(w);
      if (id == IDC_LIST)
        g_app->loadEditor();
      else if (id == IDC_CONSUMABLE_LIST)
        g_app->loadConsumable();
      else if (id >= IDC_VAR_LIST_BASE && id < IDC_VAR_LIST_BASE + 3)
        g_app->loadVariable(id - IDC_VAR_LIST_BASE);
      else
        g_app->command(w);
    } else {
      g_app->command(w);
    }
    return 0;
  }
  if (m == WM_CLOSE) {
    g_app->stopTimers();
    g_app->session.detach();
    DestroyWindow(h);
    return 0;
  }
  if (m == WM_DESTROY) {
    g_app->hwnd = nullptr;
    delete g_app;
    g_app = nullptr;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}
int runWindow(HINSTANCE instance) {
  INITCOMMONCONTROLSEX c{sizeof(c), ICC_TAB_CLASSES};
  InitCommonControlsEx(&c);
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = proc;
  wc.lpszClassName = L"BanshouNativeWindow";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
  RegisterClassW(&wc);
  HWND h =
      CreateWindowExW(0, wc.lpszClassName, L"曹操传扳手 C++ 原生版",
                      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                      100, 100, 700, 530, nullptr, nullptr, instance, nullptr);
  if (!h)
    return int(GetLastError());
  ShowWindow(h, SW_SHOW);
  UpdateWindow(h);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return int(msg.wParam);
}
} // namespace banshou
