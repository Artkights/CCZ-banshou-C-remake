#include "win32_business.hpp"
#include "diy.hpp"
#include "image.hpp"
#include "platform.hpp"
#include "remote.hpp"
#include "variables.hpp"

#include <commdlg.h>
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace banshou {
namespace {

constexpr size_t kPersonCount = 1024;
constexpr size_t kBattleCount = 250;

std::wstring fromUtf8(const std::string &value) { return wide(value); }

std::wstring gameText(const std::shared_ptr<MemorySession> &session,
                      uint32_t address, size_t width) {
  const auto bytes = session->read(address, width);
  const auto end = std::find(bytes.begin(), bytes.end(), uint8_t(0));
  if (end == bytes.begin())
    return {};
  const int count = int(end - bytes.begin());
  for (UINT codePage : {UINT(54936), UINT(936), UINT(CP_UTF8)}) {
    const int needed = MultiByteToWideChar(
        codePage, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCCH>(bytes.data()),
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

std::wstring legacyTableText(const Bytes &data, size_t offset, size_t width) {
  if (offset >= data.size())
    return {};
  const size_t count = std::min(width, data.size() - offset);
  const auto end = std::find(data.begin() + offset, data.begin() + offset + count,
                             uint8_t(0));
  const int length = int(end - (data.begin() + offset));
  if (length <= 0)
    return {};
  for (UINT codePage : {UINT(54936), UINT(936), UINT(CP_UTF8)}) {
    const int needed = MultiByteToWideChar(
        codePage, MB_ERR_INVALID_CHARS,
        reinterpret_cast<LPCCH>(data.data() + offset), length, nullptr, 0);
    if (!needed)
      continue;
    std::wstring out(size_t(needed), L'\0');
    MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                        reinterpret_cast<LPCCH>(data.data() + offset), length,
                        out.data(), needed);
    return out;
  }
  return {};
}

std::wstring hexAddress(uint32_t value) {
  std::wostringstream stream;
  stream << L"0x" << std::hex << std::uppercase << value;
  return stream.str();
}

std::wstring nativePersonNameImpl(const std::shared_ptr<MemorySession> &session,
                                  const EngineProfile &profile, uint16_t id) {
  const auto base = profile.address("person_names", 0);
  const auto stride = profile.limit("person_name_stride", 16);
  const auto width = profile.limit("person_name_size", 8);
  if (!base)
    return L"人物" + std::to_wstring(id);
  try {
    auto name = gameText(session,
                         checkedAddress(base, uint64_t(id) * stride, width),
                         width);
    if (name.empty() || name == L"非法字符")
      name = L"人物" + std::to_wstring(id);
    return name;
  } catch (...) {
    return L"人物" + std::to_wstring(id);
  }
}

const char *const kPatchKeys[] = {
    "control_friendly", "control_all_factions", "control_no_ai",
    "control_move_after_wait", "control_move_255", "control_pass_through",
    "control_auto_return"};

}  // namespace

std::wstring warehouseRowText(uint16_t item, uint32_t itemCount,
                              const std::wstring &name) {
  if (item == 0xff)
    return L"空";
  if (item >= itemCount)
    return L"无效编号：" + std::to_wstring(item);
  return std::to_wstring(item) + L"：" + name;
}

std::wstring Win32Business::nativePersonName(uint16_t id) const {
  return nativePersonNameImpl(session(), profile(), id);
}

void Win32Business::populateCatalogs() {
  auto session = this->session();
  if (!session)
    return;
  const auto profile = this->profile();
  const auto itemCount = std::min<uint32_t>(profile.limit("item_count", 255), 255);

  window_.clearControl(L"item_input_1");
  for (uint32_t i = 0; i < itemCount; ++i)
    window_.addCombo(L"item_input_1",
                     std::to_wstring(i) + L"：" + nativeItemName(i), i);
  window_.addCombo(L"item_input_1", L"空", 0xff);
  window_.setComboIndex(L"item_input_1", int(itemCount));

  window_.clearControl(L"data_input_31");
  const auto jobs = profile.limit("effect_job_count", 80);
  const auto jobNames = profile.address("job_names", 0);
  for (uint32_t i = 0; i < jobs; ++i) {
    std::wstring name;
    if (jobNames) {
      try {
        name = gameText(session,
                        checkedAddress(jobNames, uint64_t(i) * 16, 16), 15);
      } catch (...) {
      }
    }
    if (name.empty())
      name = L"兵种" + std::to_wstring(i);
    window_.addCombo(L"data_input_31", std::to_wstring(i) + L"：" + name, i);
  }
  window_.addCombo(L"data_input_31", L"空", 0xff);

  for (const wchar_t *name : {L"power_input_1_9", L"power_input_1_11"}) {
    window_.clearControl(name);
    for (uint32_t i = 0; i < jobs; ++i)
      window_.addCombo(name, L"兵种" + std::to_wstring(i), i);
    window_.addCombo(name, L"空", 0xff);
  }
  updateEffectLayout();
}

std::wstring Win32Business::nativeItemName(uint16_t id) const {
  auto session = this->session();
  if (!session)
    return L"物品" + std::to_wstring(id);
  const auto table = profile().address("item_table", 0);
  if (!table)
    return L"物品" + std::to_wstring(id);
  try {
    auto name = gameText(
        session,
        checkedAddress(table, uint64_t(id) * 25,
                       profile().limit("item_name_size", 12)),
        profile().limit("item_name_size", 12));
    if (name.empty())
      name = L"物品" + std::to_wstring(id);
    return name;
  } catch (...) {
    return L"物品" + std::to_wstring(id);
  }
}

void Win32Business::refreshPeople() {
  auto session = this->session();
  if (!session)
    return;
  if (window_.listCount(L"listview_data") == 0) {
    const auto count = profile().limit("person_count", 1024);
    const auto nameBase = profile().address("person_names", 0);
    for (uint32_t id = 0; id < count && id < kPersonCount; ++id) {
      window_.addList(L"listview_data",
                      std::to_wstring(id) + L"：" +
                          nativePersonNameImpl(session, profile(), uint16_t(id)),
                      id);
    }
    if (window_.listSelection(L"listview_data") < 0 &&
        window_.listCount(L"listview_data") > 0)
      SendMessageW(window_.control(L"listview_data"), LB_SETCURSEL, 0, 0);
  }
  loadPerson();
}

void Win32Business::loadPerson() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_data");
  if (!session || row < 0)
    return;
  const auto id = uint16_t(window_.listData(L"listview_data", row));
  const auto record = PersonService(session, profile()).read(id);
  const std::array<uint64_t, 38> values = {
      record.r,      record.s,          record.face,       record.strength,
      record.defense, record.intelligence, record.agility, record.luck,
      record.hp,     record.mp,         0,                 record.level,
      record.exp,    record.baseStats[0], record.baseStats[1],
      record.baseStats[2], record.baseStats[3], record.baseStats[4],
      record.appearance, record.retreat, record.merits[0], record.merits[1],
      record.merits[2], record.merits[3], record.merits[4], record.field28,
      record.field06, record.field44, record.field46, record.kills, 0, 0,
      record.weaponLevel, record.weaponExp, 0, record.armorLevel,
      record.armorExp, 0};
  for (size_t i = 0; i < values.size(); ++i) {
    const int index = int(i + 1);
    if (index == 11 || index == 31 || index == 32 || index == 35 ||
        index == 38)
      continue;
    window_.setText((L"data_input_" + std::to_wstring(index)).c_str(),
                    values[i]);
  }
  window_.setChecked(L"data_input_11", record.ours == 0);
  window_.selectComboData(L"data_input_31", record.job);
  window_.selectComboData(L"data_input_32", record.weapon);
  window_.selectComboData(L"data_input_35", record.armor);
  window_.selectComboData(L"data_input_38", record.accessory);
  const bool meritAvailable = id < profile().limit("merit_count", 102);
  for (wchar_t suffix : {'1', '2', '3', '4', '5'}) {
    const std::wstring name = std::wstring(L"data_input_2") + suffix;
    window_.setEnabled(name.c_str(), meritAvailable);
  }
}

void Win32Business::savePerson() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_data");
  if (!session || row < 0)
    throw Error("未选择人物");
  const auto id = uint16_t(window_.listData(L"listview_data", row));
  auto record = PersonService(session, profile()).read(id);
  const auto get = [&](int index, uint64_t maximum) {
    return window_.number((L"data_input_" + std::to_wstring(index)).c_str(),
                          maximum, "人物字段");
  };
  record.r = uint16_t(get(1, 0xffff));
  record.s = uint16_t(get(2, 0xffff));
  record.face = uint16_t(get(3, 0xffff));
  record.strength = uint16_t(get(4, 0xffff));
  record.defense = uint16_t(get(5, 0xffff));
  record.intelligence = uint16_t(get(6, 0xffff));
  record.agility = uint16_t(get(7, 0xffff));
  record.luck = uint16_t(get(8, 0xffff));
  record.hp = uint32_t(get(9, UINT32_MAX));
  const bool expanded = profile().is66() ||
                        profile().version == EngineVersion::V64 ||
                        profile().version == EngineVersion::V65 ||
                        profile().version == EngineVersion::V63MP;
  record.mp = uint16_t(get(10, expanded ? 0xffff : 0xff));
  record.ours = window_.checked(L"data_input_11") ? 0 : 0xff;
  record.level = uint8_t(get(12, 0xff));
  record.exp = uint8_t(get(13, 0xff));
  for (size_t i = 0; i < record.baseStats.size(); ++i)
    record.baseStats[i] = uint8_t(get(14 + int(i), 0xff));
  record.appearance = uint8_t(get(19, 0xff));
  record.retreat = uint8_t(get(20, 0xff));
  if (id < profile().limit("merit_count", 102))
    for (size_t i = 0; i < record.merits.size(); ++i)
      record.merits[i] = uint16_t(get(21 + int(i), 0xffff));
  record.field28 = uint8_t(get(26, 0xff));
  record.field06 = uint16_t(get(27, 0xffff));
  record.field44 = uint16_t(get(28, 0xffff));
  record.field46 = uint16_t(get(29, 0xffff));
  record.kills = uint16_t(get(30, 0xffff));
  record.weaponLevel = uint8_t(get(33, 0xff));
  record.weaponExp = uint8_t(get(34, 0xff));
  record.armorLevel = uint8_t(get(36, 0xff));
  record.armorExp = uint8_t(get(37, 0xff));
  record.job = uint8_t(window_.comboData(L"data_input_31", 0xff));
  record.weapon = uint8_t(window_.comboData(L"data_input_32", 0xff));
  record.armor = uint8_t(window_.comboData(L"data_input_35", 0xff));
  record.accessory = uint8_t(window_.comboData(L"data_input_38", 0xff));

  if (profile().version == EngineVersion::V64 ||
      profile().version == EngineVersion::V65 ||
      profile().version == EngineVersion::V66) {
    const auto equipmentEnd = profile().limit("equipment_count", 150) - 1;
    const auto validate = [&](uint8_t item, uint32_t first, uint32_t last,
                              const char *label) {
      if (item != 0xff && (item < first || item > last))
        throw Error(std::string(label) + "不能使用消耗品或其他类别物品");
    };
    validate(record.weapon, 0, std::min<uint32_t>(69, equipmentEnd), "武器");
    validate(record.armor, 70, std::min<uint32_t>(108, equipmentEnd), "防具");
    validate(record.accessory, 109, equipmentEnd, "辅助");
  }
  PersonService(session, profile()).write(id, record);
  loadPerson();
  window_.setStatus(L"人物已保存");
}

void Win32Business::recalculatePerson() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_data");
  if (!session || row < 0)
    throw Error("未选择人物");
  const auto id = window_.listData(L"listview_data", row);
  const auto base = session->u(profile().address("person_pointer", 0x004CEA00), 4);
  const auto address = checkedAddress(base, uint64_t(id) * 0x48, 0x48);
  const auto before = session->read(address, 0x48);
  RemoteServices(session, profile()).recalculate(address);
  const auto after = session->read(address, 0x48);
  std::vector<std::string> changed;
  const struct Range {
    size_t begin, end;
    const char *label;
  } ranges[] = {{0x1B, 0x21, "HP/MP"},
                {0x21, 0x26, "基础五维"},
                {0x2B, 0x2C, "兵种"},
                {0x2C, 0x2D, "等级"}};
  for (const auto &range : ranges)
    if (!std::equal(before.begin() + range.begin, before.begin() + range.end,
                    after.begin() + range.begin))
      changed.push_back(range.label);
  if (before[0x2D] != after[0x2D] &&
      !(before[0x2C] >= 60 && after[0x2D] == 0xff))
    changed.push_back("经验");
  loadPerson();
  if (!changed.empty()) {
    std::string message = "人物能力重算意外修改了";
    for (size_t i = 0; i < changed.size(); ++i) {
      if (i)
        message += "、";
      message += changed[i];
    }
    throw Error(message);
  }
  window_.setStatus(L"人物能力已重新计算");
}


namespace {
size_t selectedSlot(const Win32MainWindow &window, const wchar_t *listName) {
  const int row = window.listSelection(listName);
  return row < 0 ? SIZE_MAX : size_t(window.listData(listName, row));
}

bool expandedHealth(const std::shared_ptr<MemorySession> &session,
                    const EngineProfile &profile) {
  return profile.is66() ||
         ((profile.version == EngineVersion::V64 ||
           profile.version == EngineVersion::V65) &&
          session->u(0x004240CA, 1, false) == 0xfc);
}
}  // namespace

void Win32Business::refreshBattle(bool full) {
  auto session = this->session();
  if (!session)
    return;
  (void)full;
  const auto profile = this->profile();
  const auto total = profile.limit("battle_ours", 20) +
                     profile.limit("battle_friends", 40) +
                     profile.limit("battle_enemies", 190);
  const int previous = window_.listSelection(L"listview_war");
  window_.clearControl(L"listview_war");
  BattleService battle(session, profile);
  for (uint32_t slot = 0; slot < total && slot < kBattleCount; ++slot) {
    const auto record = battle.read(slot);
    if (record.dataId == 0xffff)
      continue;
    window_.addList(L"listview_war",
                    std::to_wstring(slot) + L"：" +
                        std::to_wstring(record.dataId) + L"：" +
                        nativePersonNameImpl(session, profile, record.dataId),
                    slot);
  }
  if (window_.listCount(L"listview_war") > 0)
    SendMessageW(window_.control(L"listview_war"), LB_SETCURSEL,
                 WPARAM(std::min(previous, window_.listCount(L"listview_war") - 1)), 0);
  loadBattle();
}

void Win32Business::refreshBattleStrategy() {
  const bool ours = window_.checked(L"war_kind_1");
  const int strategy = window_.comboIndex(L"war_input_11");
  const bool target = strategy == 3 || strategy == 5;
  for (const wchar_t *name : {L"war_11", L"war_input_11", L"war_13",
                              L"war_input_13", L"war_14", L"war_input_14",
                              L"war_36", L"war_input_36", L"war_37",
                              L"war_input_37"})
    window_.setVisible(name, !ours);
  window_.setVisible(L"war_12", !ours && target);
  window_.setVisible(L"war_input_12", !ours && target);
}

void Win32Business::loadBattle() {
  auto session = this->session();
  const size_t slot = selectedSlot(window_, L"listview_war");
  if (!session || slot == SIZE_MAX)
    return;
  const auto profile = this->profile();
  const auto total = profile.limit("battle_ours", 20) +
                     profile.limit("battle_friends", 40) +
                     profile.limit("battle_enemies", 190);
  BattleService battle(session, profile);
  const auto record = battle.read(slot);
  window_.selectComboData(L"war_input_1", record.dataId);
  window_.setText(L"war_input_2", record.hp);
  window_.setText(L"war_input_3", record.mp);
  window_.setText(L"war_input_4", record.sp);
  const bool expanded = expandedHealth(session, profile);
  for (int i = 0; i < 4; ++i) {
    const std::wstring digit = L"war_input_" + std::to_wstring(51 + i);
    const std::wstring box = L"war_input_" + std::to_wstring(5 + i);
    window_.setVisible(digit.c_str(), expanded);
    window_.setVisible(box.c_str(), !expanded);
    if (expanded)
      window_.setText(digit.c_str(), (record.health >> (i * 2)) & 3);
    else
      window_.setChecked(box.c_str(), (record.health & (2u << i)) != 0);
  }
  window_.setText(L"war_input_9", record.x);
  window_.setText(L"war_input_10", record.y);
  window_.setChecked(L"war_input_15", (record.action & 6) != 0);
  window_.setComboIndex(L"war_input_16", 0);
  const wchar_t *buffCombos[] = {L"war_input_17", L"war_input_18",
                                 L"war_input_19", L"war_input_20",
                                 L"war_input_21", L"war_input_22"};
  for (size_t i = 0; i < 6; ++i)
    window_.setComboIndex(buffCombos[i],
                          record.buffs[i] < 3 ? 0 : (record.buffs[i] == 3 ? 1 : 2));
  window_.setComboIndex(L"war_input_23", 0);

  int strategy = 0;
  if (record.ai == 1) strategy = 1;
  else if (record.ai == 2) strategy = 2;
  else if (record.ai == 0) strategy = record.targetSlot == 0xff ? 4 : 3;
  else if (record.ai == 4) strategy = record.targetSlot == 0xff ? 6 : 5;
  window_.setComboIndex(L"war_input_11", strategy);
  window_.setText(L"war_input_13", record.strategyX);
  window_.setText(L"war_input_14", record.strategyY);
  window_.setText(L"war_input_36", record.targetX);
  window_.setText(L"war_input_37", record.targetY);

  window_.clearControl(L"war_input_12");
  int targetIndex = -1;
  for (uint32_t i = 0; i < total; ++i) {
    const auto candidate = battle.read(i);
    if (candidate.dataId == 0xffff)
      continue;
    const int row = window_.addList(
        L"war_input_12",
        std::to_wstring(candidate.displaySlot) + L"：" +
            nativePersonNameImpl(session, profile, candidate.dataId),
        i);
    if (i == record.targetSlot)
      targetIndex = row;
  }
  window_.setComboIndex(L"war_input_12", targetIndex >= 0 ? targetIndex : 0);
  refreshBattleStrategy();
  window_.setText(L"war_input_38", record.field22);
  window_.setText(L"war_input_39", record.field23);
  const wchar_t *expandedEdits[] = {L"war_input_40", L"war_input_44",
                                    L"war_input_41", L"war_input_45",
                                    L"war_input_42", L"war_input_43"};
  const auto stride = profile.limit("battle_stride", profile.is66() ? 0x30 : 0x24);
  for (size_t i = 0; i < 6; ++i) {
    window_.setText(expandedEdits[i], record.expanded[i]);
    window_.setEnabled(expandedEdits[i], stride > 0x24 + i * 2);
  }
  window_.setChecked(L"war_input_46", record.ai == 7);

  const auto state = BattleStateService(session, profile).read();
  window_.setText(L"war_input_24", state.turnLimit);
  window_.setText(L"war_input_25", state.currentTurn);
  window_.setText(L"war_input_26", state.nextScenario);
  window_.setComboIndex(L"war_input_27", state.weather);
  const bool revivable = record.dataId != 0xffff && record.hp == 0 &&
                         record.visibility == 3;
  window_.setEnabled(L"war_life", revivable);
  window_.setEnabled(L"war_input_9", revivable);
  window_.setEnabled(L"war_input_10", revivable);
}

void Win32Business::saveBattle() {
  auto session = this->session();
  const size_t slot = selectedSlot(window_, L"listview_war");
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  const auto profile = this->profile();
  auto service = BattleService(session, profile);
  const bool fullSave =
      (session->u(checkedAddress(profile.address("save_state", 0x004B0770), 6, 1),
                  1) &
       1) != 0;
  auto record = service.read(slot);
  const auto oldDirection = record.direction;
  record.dataId = uint16_t(window_.comboData(L"war_input_1", 0xffff));
  record.hp = uint32_t(window_.number(L"war_input_2", UINT32_MAX, "当前HP"));
  record.mp = uint32_t(window_.number(L"war_input_3", UINT32_MAX, "当前MP"));
  record.sp = uint8_t(window_.number(L"war_input_4", 0xff, "当前SP"));
  record.x = uint8_t(window_.number(L"war_input_9", 0xff, "X坐标"));
  record.y = uint8_t(window_.number(L"war_input_10", 0xff, "Y坐标"));
  record.action = window_.checked(L"war_input_15")
                      ? uint8_t(record.action | 6)
                      : uint8_t(record.action & 0xf8);
  if (window_.comboIndex(L"war_input_16") > 0)
    record.direction = uint8_t(window_.comboIndex(L"war_input_16") - 1);

  const bool expanded = expandedHealth(session, profile);
  record.health = 0;
  if (expanded) {
    for (int i = 0; i < 4; ++i) {
      const std::wstring digit = L"war_input_" + std::to_wstring(51 + i);
      record.health |= uint8_t((window_.number(digit.c_str(), 3, "健康状态") & 3)
                               << (i * 2));
    }
  } else {
    for (int i = 0; i < 4; ++i) {
      const std::wstring box = L"war_input_" + std::to_wstring(5 + i);
      if (window_.checked(box.c_str()))
        record.health |= uint8_t(2u << i);
    }
  }
  const wchar_t *buffCombos[] = {L"war_input_17", L"war_input_18",
                                 L"war_input_19", L"war_input_20",
                                 L"war_input_21", L"war_input_22"};
  static const uint8_t buffValues[] = {1, 3, 6};
  for (size_t i = 0; i < 6; ++i)
    record.buffs[i] =
        buffValues[std::clamp(window_.comboIndex(buffCombos[i]), 0, 2)];
  if (window_.comboIndex(L"war_input_23") > 0) {
    const auto all = buffValues[std::clamp(
        window_.comboIndex(L"war_input_23") - 1, 0, 2)];
    record.buffs.fill(all);
  }
  if (!window_.checked(L"war_kind_1")) {
    record.strategyX = uint8_t(window_.number(L"war_input_13", 0xff, "方针X"));
    record.strategyY = uint8_t(window_.number(L"war_input_14", 0xff, "方针Y"));
    record.targetX = uint8_t(window_.number(L"war_input_36", 0xff, "目标X"));
    record.targetY = uint8_t(window_.number(L"war_input_37", 0xff, "目标Y"));
    static const uint8_t aiValues[] = {3, 1, 2, 0, 0, 4, 4};
    const int strategy = std::clamp(window_.comboIndex(L"war_input_11"), 0, 6);
    record.ai = aiValues[strategy];
    if (strategy == 3 || strategy == 5)
      record.targetSlot = uint8_t(window_.comboData(L"war_input_12", 0));
    else if (strategy == 4 || strategy == 6)
      record.targetSlot = 0xff;
    if (window_.checked(L"war_kind_2") && window_.checked(L"war_input_46"))
      record.ai = 7;
  }
  record.field22 = uint8_t(window_.number(L"war_input_38", 0xff, "字段22"));
  record.field23 = uint8_t(window_.number(L"war_input_39", 0xff, "字段23"));
  const wchar_t *expandedEdits[] = {L"war_input_40", L"war_input_44",
                                    L"war_input_41", L"war_input_45",
                                    L"war_input_42", L"war_input_43"};
  const wchar_t *order[] = {L"war_input_40", L"war_input_41", L"war_input_42",
                            L"war_input_43", L"war_input_44", L"war_input_45"};
  const uint8_t *slots[] = {&record.expanded[0], &record.expanded[2],
                            &record.expanded[4], &record.expanded[1],
                            &record.expanded[3], &record.expanded[5]};
  for (size_t i = 0; i < 6; ++i) {
    const HWND handle = window_.control(order[i]);
    if (IsWindowEnabled(handle))
      *const_cast<uint8_t *>(slots[i]) =
          uint8_t(window_.number(order[i], 0xff, "战场扩展字段"));
  }
  auto state = BattleStateService(session, profile).read();
  state.turnLimit = uint8_t(window_.number(L"war_input_24", 0xff, "回合上限"));
  state.currentTurn = uint8_t(window_.number(L"war_input_25", 0xff, "当前回合"));
  state.nextScenario = uint8_t(window_.number(L"war_input_26", 128, "下一关"));
  state.weather = uint8_t(std::clamp(window_.comboIndex(L"war_input_27"), 0, 4));

  if (!fullSave) {
    service.write(slot, record);
    refreshBattle(true);
    window_.setStatus(L"当前保存状态仅写入 HP/MP");
    return;
  }
  service.writePage(slot, record, state);
  if (oldDirection != record.direction)
    RemoteServices(session, profile).changeDirection(record.direction,
                                                     record.dataId);
  window_.setStatus(L"战场记录已保存");
}

void Win32Business::reviveBattleUnit() {
  auto session = this->session();
  const size_t slot = selectedSlot(window_, L"listview_war");
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  auto service = BattleService(session, profile());
  auto record = service.read(slot);
  if (record.dataId == 0xffff || record.hp || record.visibility != 3)
    throw Error("当前单位不可复活");
  const auto direction =
      uint32_t(window_.number(L"war_input_9", 0xff, "X坐标"));
  const auto x = uint32_t(window_.number(L"war_input_9", 0xff, "X坐标"));
  const auto y = uint32_t(window_.number(L"war_input_10", 0xff, "Y坐标"));
  (void)direction;
  record.hp = 1;
  service.write(slot, record);
  RemoteServices(session, profile()).revive(record.direction, x, y, record.dataId);
  window_.setStatus(L"战场单位已复活");
  refreshBattle(true);
}

void Win32Business::killBattleArea() {
  auto session = this->session();
  if (!session)
    throw Error("尚未附加游戏");
  const auto x1 = window_.number(L"war_input_41", 0xff, "区域X1");
  const auto y1 = window_.number(L"war_input_42", 0xff, "区域Y1");
  const auto x2 = window_.number(L"war_input_43", 0xff, "区域X2");
  const auto y2 = window_.number(L"war_input_44", 0xff, "区域Y2");
  BattleService service(session, profile());
  std::vector<std::pair<size_t, BattleRecord>> rows;
  for (size_t slot = 0; slot < kBattleCount; ++slot) {
    auto record = service.read(slot);
    if (record.dataId == 0xffff || record.visibility < 2)
      continue;
    if (record.x >= x1 && record.x <= x2 && record.y >= y1 && record.y <= y2) {
      record.hp = 0;
      rows.emplace_back(slot, record);
    }
  }
  service.writeRows(rows);
  refreshBattle(true);
  window_.setStatus(L"区域灭敌完成");
}

void Win32Business::connectBattlePerson() {
  auto session = this->session();
  const size_t slot = selectedSlot(window_, L"listview_war");
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  const auto record = BattleService(session, profile()).read(slot);
  if (record.dataId == 0xffff)
    throw Error("当前战场单位没有关联人物");
  const auto count = window_.listCount(L"listview_data");
  for (int row = 0; row < count; ++row) {
    if (window_.listData(L"listview_data", row) == record.dataId) {
      SendMessageW(window_.control(L"listview_data"), LB_SETCURSEL, WPARAM(row), 0);
      window_.showPage(1);
      window_.setStatus(L"已连接人物 " + std::to_wstring(record.dataId));
      return;
    }
  }
  throw Error("人物列表中没有找到该编号");
}

void Win32Business::addBattleLock() {
  auto session = this->session();
  const size_t slot = selectedSlot(window_, L"listview_war");
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  const auto record = BattleService(session, profile()).read(slot);
  const int row = window_.addList(
      L"listview_lock",
      std::to_wstring(slot) + L"：" + std::to_wstring(record.dataId), slot);
  if (row >= 0)
    window_.setListData(L"listview_lock", row, slot);
  SetTimer(window_.handle(), 3, 1000, nullptr);
  window_.setStatus(L"已增加锁定");
}

void Win32Business::removeBattleLock() {
  const int row = window_.listSelection(L"listview_lock");
  if (row < 0)
    return;
  SendMessageW(window_.control(L"listview_lock"), LB_DELETESTRING, WPARAM(row), 0);
  if (window_.listCount(L"listview_lock") == 0)
    KillTimer(window_.handle(), 3);
  window_.setStatus(L"已移除锁定");
}

void Win32Business::runBattleLocks() {
  auto session = this->session();
  if (!session)
    return;
  auto service = BattleService(session, profile());
  const int count = window_.listCount(L"listview_lock");
  for (int row = 0; row < count; ++row) {
    const size_t slot = size_t(window_.listData(L"listview_lock", row));
    if (slot >= kBattleCount)
      continue;
    auto record = service.read(slot);
    record.hp = uint32_t(window_.number(L"war_input_2", UINT32_MAX, "HP"));
    record.mp = uint32_t(window_.number(L"war_input_3", UINT32_MAX, "MP"));
    service.write(slot, record);
  }
}

void Win32Business::runAutoRevive() {
  auto session = this->session();
  if (!session)
    return;
  auto service = BattleService(session, profile());
  for (size_t slot = 0; slot < kBattleCount; ++slot) {
    auto record = service.read(slot);
    if (record.dataId == 0xffff || record.hp || record.visibility != 3)
      continue;
    record.hp = 1;
    service.write(slot, record);
    RemoteServices(session, profile()).revive(record.direction, record.x,
                                             record.y, record.dataId);
  }
}


void Win32Business::refreshWarehouse() {
  auto session = this->session();
  if (!session)
    return;
  WarehouseService service(session, profile());
  const auto itemCount = profile().limit("item_count", 255);
  const auto records = service.readRecords(200);
  window_.clearControl(L"listview_item");
  for (size_t slot = 0; slot < records.size(); ++slot) {
    const auto &record = records[slot];
    window_.addList(L"listview_item",
                    warehouseRowText(record.item, itemCount,
                                     nativeItemName(record.item)),
                    slot);
  }
  if (window_.listCount(L"listview_item") > 0 &&
      window_.listSelection(L"listview_item") < 0)
    SendMessageW(window_.control(L"listview_item"), LB_SETCURSEL, 0, 0);
  window_.clearControl(L"listview_item_2");
  for (size_t i = 0; i < service.consumableCount(); ++i) {
    const auto id = service.consumableItemId(i);
    window_.addList(L"listview_item_2",
                    std::to_wstring(id) + L"：" + nativeItemName(id), i);
  }
  window_.setText(L"item_input_4",
                  uint64_t(service.readConsumable(
                      size_t(std::max(0, window_.listSelection(L"listview_item_2"))))));
  window_.setText(L"item_input_5", session->u(profile().address("money", 0x004B077C), 4));
  window_.setText(L"item_input_6", session->u(profile().address("alignment", 0x004B0782), 1));
  window_.setText(L"item_input_7", session->u(profile().address("merit_pool", 0x00505F40), 4));
}

void Win32Business::saveWarehouse() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_item");
  if (!session || row < 0)
    throw Error("未选择仓库槽位");
  const size_t slot = size_t(window_.listData(L"listview_item", row));
  WarehousePageEdit edit;
  edit.index = slot;
  edit.writeRecord = true;
  edit.record.item = uint8_t(window_.comboData(L"item_input_1", 0xff));
  edit.record.level = uint8_t(window_.number(L"item_input_2", 9, "装备等级"));
  edit.record.experience =
      uint8_t(window_.number(L"item_input_3", 255, "装备经验"));
  const int consumable = window_.listSelection(L"listview_item_2");
  if (consumable >= 0) {
    edit.writeConsumable = true;
    edit.consumableIndex = size_t(window_.listData(L"listview_item_2", consumable));
    edit.consumableValue =
        uint8_t(window_.number(L"item_input_4", 0xff, "道具数量"));
  }
  edit.writeGlobals = true;
  edit.money = uint32_t(window_.number(L"item_input_5", UINT32_MAX, "金钱"));
  edit.alignment = uint8_t(window_.number(L"item_input_6", 255, "忠奸度"));
  edit.meritPool = uint32_t(window_.number(L"item_input_7", UINT32_MAX, "功勋池"));
  WarehouseService(session, profile()).writePage(edit);
  refreshWarehouse();
  window_.setStatus(L"仓库已保存");
}

void Win32Business::warehouseBulk(int action) {
  auto session = this->session();
  if (!session)
    throw Error("尚未附加游戏");
  WarehouseService service(session, profile());
  if (action == 1 || action == 2) {
    const auto level = uint8_t(window_.number(L"item_input_2", 9, "装备等级"));
    const auto experience =
        uint8_t(window_.number(L"item_input_3", 255, "装备经验"));
    service.addAllTreasures(level, experience);
    window_.setStatus(action == 1 ? L"已放入全部宝物" : L"已放入全部道具");
  } else {
    service.clearWarehouse();
    window_.setStatus(L"仓库已清空");
  }
  refreshWarehouse();
}

void Win32Business::updateEffectLayout() {
  const bool extended = session() &&
                        profile().limit("effect_stride", 8) >= 16;
  const wchar_t *extendedOnly[] = {L"power_input_1_5", L"power_input_1_6",
                                   L"power_input_1_7", L"power_input_1_8",
                                   L"power_input_1_10", L"power_input_1_12"};
  for (const wchar_t *name : extendedOnly)
    window_.setVisible(name, extended);
  if (extended) {
    window_.setRect(L"power_input_1_1", 185, 40, 96, 25);
    window_.setRect(L"power_input_1_2", 355, 40, 96, 25);
    window_.setRect(L"power_input_1_3", 525, 40, 96, 25);
    window_.setRect(L"power_input_1_4", 185, 80, 96, 25);
    window_.setRect(L"power_input_1_9", 355, 80, 96, 25);
    window_.setRect(L"power_input_1_11", 525, 80, 96, 25);
    window_.setRect(L"power_input_1_5", 285, 40, 36, 25);
    window_.setRect(L"power_input_1_6", 455, 40, 36, 25);
    window_.setRect(L"power_input_1_7", 625, 40, 36, 25);
    window_.setRect(L"power_input_1_8", 285, 80, 36, 25);
    window_.setRect(L"power_input_1_10", 455, 80, 36, 25);
    window_.setRect(L"power_input_1_12", 625, 80, 36, 25);
  } else {
    window_.setRect(L"power_input_1_1", 185, 40, 136, 25);
    window_.setRect(L"power_input_1_2", 355, 40, 136, 25);
    window_.setRect(L"power_input_1_3", 525, 40, 136, 25);
    window_.setRect(L"power_input_1_4", 185, 80, 136, 25);
    window_.setRect(L"power_input_1_9", 355, 80, 136, 25);
  }
}

void Win32Business::refreshEffects() {
  auto session = this->session();
  if (!session)
    return;
  const auto count = profile().limit("effect_count", 80);
  const int previous = window_.listSelection(L"listview_item_3");
  window_.clearControl(L"listview_item_3");
  for (uint32_t id = 0; id < count; ++id)
    window_.addList(L"listview_item_3", std::to_wstring(id) + L"：天赋" +
                                            std::to_wstring(id), id);
  if (window_.listCount(L"listview_item_3") > 0)
    SendMessageW(window_.control(L"listview_item_3"), LB_SETCURSEL,
                 WPARAM(std::min(previous, window_.listCount(L"listview_item_3") - 1)), 0);
  updateEffectLayout();
  loadEffect();
}

void Win32Business::loadEffect() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_item_3");
  if (!session || row < 0)
    return;
  const auto id = size_t(window_.listData(L"listview_item_3", row));
  const auto record = EffectService(session, profile()).read(id);
  if (profile().limit("effect_stride", 8) >= 16) {
    const wchar_t *characters[] = {L"power_input_1_1", L"power_input_1_2",
                                   L"power_input_1_3", L"power_input_1_4"};
    const wchar_t *values[] = {L"power_input_1_5", L"power_input_1_6",
                               L"power_input_1_7", L"power_input_1_8"};
    for (size_t i = 0; i < 4; ++i) {
      window_.selectComboData(characters[i], record.characters[i]);
      window_.setText(values[i], record.characterValues[i]);
    }
    window_.selectComboData(L"power_input_1_9", record.jobs[0]);
    window_.selectComboData(L"power_input_1_11", record.jobs[1]);
    window_.setText(L"power_input_1_10", record.jobValues[0]);
    window_.setText(L"power_input_1_12", record.jobValues[1]);
  } else {
    window_.selectComboData(L"power_input_1_1", record.characters[0]);
    window_.selectComboData(L"power_input_1_2", record.characters[1]);
    window_.selectComboData(L"power_input_1_3", record.characters[2]);
    window_.selectComboData(L"power_input_1_4", record.jobs[0]);
    window_.setText(L"power_input_1_5", record.jobValues[0]);
  }
}

void Win32Business::saveEffect() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_item_3");
  if (!session || row < 0)
    throw Error("未选择天赋记录");
  const auto id = size_t(window_.listData(L"listview_item_3", row));
  EffectRecord record{};
  if (profile().limit("effect_stride", 8) >= 16) {
    const wchar_t *characters[] = {L"power_input_1_1", L"power_input_1_2",
                                   L"power_input_1_3", L"power_input_1_4"};
    const wchar_t *values[] = {L"power_input_1_5", L"power_input_1_6",
                               L"power_input_1_7", L"power_input_1_8"};
    for (size_t i = 0; i < 4; ++i) {
      record.characters[i] = uint16_t(window_.comboData(characters[i], 0xff));
      record.characterValues[i] =
          uint8_t(window_.number(values[i], 0xff, "天赋值"));
    }
    record.jobs[0] = uint8_t(window_.comboData(L"power_input_1_9", 0xff));
    record.jobs[1] = uint8_t(window_.comboData(L"power_input_1_11", 0xff));
    record.jobValues[0] =
        uint8_t(window_.number(L"power_input_1_10", 0xff, "兵种天赋值"));
    record.jobValues[1] =
        uint8_t(window_.number(L"power_input_1_12", 0xff, "兵种天赋值"));
  } else {
    record.characters[0] = uint16_t(window_.comboData(L"power_input_1_1", 0xff));
    record.characters[1] = uint16_t(window_.comboData(L"power_input_1_2", 0xff));
    record.characters[2] = uint16_t(window_.comboData(L"power_input_1_3", 0xff));
    record.jobs[0] = uint8_t(window_.comboData(L"power_input_1_4", 0xff));
    record.jobValues[0] =
        uint8_t(window_.number(L"power_input_1_5", 0xff, "天赋值"));
  }
  EffectService(session, profile()).write(id, record);
  loadEffect();
  window_.setStatus(L"天赋记录已保存");
}


void Win32Business::refreshVariables() {
  auto session = this->session();
  if (!session)
    return;
  VariableService service(session, profile());
  const wchar_t *lists[] = {L"listview_var_1", L"listview_var_2",
                            L"listview_var_3"};
  const wchar_t *starts[] = {L"var_input_1", L"var_input_2", L"var_input_3"};
  const wchar_t *values[] = {L"var_input_5", L"var_input_6", L"var_input_7"};
  for (int kind = 0; kind < 3; ++kind) {
    const auto kindValue = VariableKind(kind);
    const auto maximum = service.browseCount(kindValue);
    const auto start = size_t(window_.number(
        starts[kind], maximum ? uint32_t(maximum - 1) : 0, "变量起始索引"));
    const auto cells = service.readPage(
        kindValue, start, std::min<size_t>(100, maximum - start));
    window_.clearControl(lists[kind]);
    for (size_t row = 0; row < cells.size(); ++row) {
      const auto &cell = cells[row];
      std::wstring value;
      if (kind == 0)
        value = cell.value ? L"True" : L"False";
      else if (kind == 1)
        value = std::to_wstring(int32_t(cell.value));
      else
        value = std::to_wstring(cell.value);
      window_.addList(lists[kind],
                      std::to_wstring(cell.index) + L"  " + value + L"  " +
                          hexAddress(cell.address),
                      cell.index);
    }
    if (!cells.empty())
      window_.setComboIndex(values[kind], 0);
    if (cells.empty())
      window_.setText(values[kind], L"");
    else if (kind == 0)
      window_.setText(values[kind], cells[0].value ? L"True" : L"False");
    else if (kind == 1)
      window_.setText(values[kind], int32_t(cells[0].value));
    else
      window_.setText(values[kind], cells[0].value);
  }
}

void Win32Business::saveVariable(int kind) {
  auto session = this->session();
  if (!session || kind < 0 || kind >= 3)
    throw Error("尚未附加游戏");
  const wchar_t *lists[] = {L"listview_var_1", L"listview_var_2",
                            L"listview_var_3"};
  const wchar_t *values[] = {L"var_input_5", L"var_input_6", L"var_input_7"};
  const int row = window_.listSelection(lists[kind]);
  if (row < 0)
    throw Error("未选择变量");
  const size_t index = size_t(window_.listData(lists[kind], row));
  VariableService service(session, profile());
  if (kind == 0) {
    const auto value = window_.text(values[kind]);
    service.writeBool(index, value == L"True" || value == L"true" ||
                                 value == L"T" || value == L"t" ||
                                 value == L"1");
  } else if (kind == 1) {
    service.writeInt(index, window_.signedNumber(values[kind], "整数变量"));
  } else {
    service.writePointer(
        index, uint32_t(window_.number(values[kind], UINT32_MAX, "指针变量")));
  }
  refreshVariables();
  window_.setStatus(L"变量已保存");
}

void Win32Business::saveBooleanPage() {
  auto session = this->session();
  if (!session)
    throw Error("尚未附加游戏");
  VariableService service(session, profile());
  const auto maximum = service.browseCount(VariableKind::Boolean);
  const HWND list = window_.control(L"listview_var_1");
  const int count = int(SendMessageW(list, LB_GETCOUNT, 0, 0));
  const HWND edit = window_.control(L"var_input_5");
  const int rows = int(SendMessageW(list, LB_GETSELCOUNT, 0, 0));
  std::vector<int> selected(size_t(std::max(rows, 0)));
  if (rows > 0)
    SendMessageW(list, LB_GETSELITEMS, WPARAM(rows),
                 reinterpret_cast<LPARAM>(selected.data()));
  const std::wstring text = window_.text(L"var_input_5");
  (void)edit;
  const bool value = text == L"True" || text == L"true" || text == L"1";
  std::vector<std::pair<size_t, bool>> changes;
  if (selected.empty()) {
    const int row = int(SendMessageW(list, LB_GETCURSEL, 0, 0));
    if (row >= 0)
      changes.emplace_back(size_t(window_.listData(L"listview_var_1", row)), value);
  } else {
    for (int row : selected)
      changes.emplace_back(size_t(window_.listData(L"listview_var_1", row)), value);
  }
  service.writeBools(changes);
  refreshVariables();
  window_.setStatus(L"已保存 " + std::to_wstring(changes.size()) + L" 项布尔变量");
  (void)maximum;
}

void Win32Business::exportBooleanArchive() {
  auto session = this->session();
  if (!session)
    throw Error("尚未附加游戏");
  VariableService service(session, profile());
  const auto count = service.writableCount(VariableKind::Boolean);
  if (!count)
    throw Error("当前画像没有可写的布尔变量域");
  std::vector<bool> values;
  values.reserve(count);
  for (size_t index = 0; index < count; ++index)
    values.push_back(service.readBool(index));
  const auto archive = encodeBooleanArchive(values, count);
  wchar_t path[MAX_PATH] = {};
  wcscpy_s(path, L"布尔变量.cczbool");
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_.handle();
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.lpstrFilter = L"布尔归档\0*.cczbool\0所有文件\0*.*\0\0";
  dialog.nFilterIndex = 1;
  dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&dialog))
    return;
  writeFile(path, archive);
  window_.setStatus(L"已全量导出 " + std::to_wstring(count) + L" 项布尔变量");
}

void Win32Business::importBooleanArchive() {
  auto session = this->session();
  if (!session)
    throw Error("尚未附加游戏");
  VariableService service(session, profile());
  const auto count = service.writableCount(VariableKind::Boolean);
  if (!count)
    throw Error("当前画像没有可写的布尔变量域");
  wchar_t path[MAX_PATH] = {};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_.handle();
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.lpstrFilter = L"布尔归档\0*.cczbool\0所有文件\0*.*\0\0";
  dialog.nFilterIndex = 1;
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&dialog))
    return;
  const auto values = decodeBooleanArchive(readFile(path), count);
  std::vector<std::pair<size_t, bool>> changed;
  for (size_t index = 0; index < count && index < values.size(); ++index)
    if (service.readBool(index) != values[index])
      changed.emplace_back(index, values[index]);
  service.writeBools(changed);
  refreshVariables();
  window_.setStatus(L"已全量导入，实际修改 " +
                    std::to_wstring(changed.size()) + L" 项");
}

namespace {
constexpr int kMenuSaveRow = 1;
constexpr int kMenuSavePage = 2;
constexpr int kMenuExport = 3;
constexpr int kMenuImport = 4;
}  // namespace

void Win32Business::showBooleanMenu(HWND source, POINT point) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kMenuSaveRow, L"保存选中行");
  AppendMenuW(menu, MF_STRING, kMenuSavePage, L"保存本页全部修改");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kMenuExport, L"全量导出…");
  AppendMenuW(menu, MF_STRING, kMenuImport, L"全量导入并保存…");
  const int command = int(TrackPopupMenu(
      menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, point.x, point.y, 0,
      source, nullptr));
  DestroyMenu(menu);
  if (command == kMenuSaveRow)
    saveVariable(0);
  else if (command == kMenuSavePage)
    saveBooleanPage();
  else if (command == kMenuExport)
    exportBooleanArchive();
  else if (command == kMenuImport)
    importBooleanArchive();
}

void Win32Business::refreshFatal() {
  auto session = this->session();
  if (!session)
    return;
  const auto count = profile().limit("fatal_count", 80);
  const int previous = window_.listSelection(L"listview_mk");
  window_.clearControl(L"listview_mk");
  for (uint32_t id = 0; id < count; ++id)
    window_.addList(L"listview_mk",
                    std::to_wstring(id) + L"：必杀" + std::to_wstring(id), id);
  if (window_.listCount(L"listview_mk") > 0)
    SendMessageW(window_.control(L"listview_mk"), LB_SETCURSEL,
                 WPARAM(std::min(previous, window_.listCount(L"listview_mk") - 1)), 0);
  loadFatal();
}

void Win32Business::loadFatal() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_mk");
  if (!session || row < 0)
    return;
  const auto id = size_t(window_.listData(L"listview_mk", row));
  const auto record = FatalService(session, profile()).read(id);
  const wchar_t *persons[] = {L"mk_input_1_1", L"mk_input_1_3",
                              L"mk_input_1_5", L"mk_input_1_7",
                              L"mk_input_1_9"};
  const wchar_t *levels[] = {L"mk_input_1_2", L"mk_input_1_4",
                             L"mk_input_1_6", L"mk_input_1_8",
                             L"mk_input_1_10"};
  for (size_t i = 0; i < 5; ++i) {
    window_.selectComboData(persons[i], record.persons[i]);
    window_.setText(levels[i], record.levels[i]);
  }
  window_.setText(L"mk_input_1_11", record.value);
}

void Win32Business::saveFatal() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_mk");
  if (!session || row < 0)
    throw Error("未选择必杀记录");
  const auto id = size_t(window_.listData(L"listview_mk", row));
  FatalRecord record{};
  const wchar_t *persons[] = {L"mk_input_1_1", L"mk_input_1_3",
                              L"mk_input_1_5", L"mk_input_1_7",
                              L"mk_input_1_9"};
  const wchar_t *levels[] = {L"mk_input_1_2", L"mk_input_1_4",
                             L"mk_input_1_6", L"mk_input_1_8",
                             L"mk_input_1_10"};
  for (size_t i = 0; i < 5; ++i) {
    record.persons[i] = uint16_t(window_.comboData(persons[i], 0xff));
    record.levels[i] = uint8_t(window_.number(levels[i], 0xff, "必杀领悟等级"));
  }
  record.value = uint8_t(window_.number(L"mk_input_1_11", 0xff, "必杀效果值"));
  FatalService(session, profile()).write(id, record);
  loadFatal();
  window_.setStatus(L"必杀记录已保存");
}

void Win32Business::refreshCurrentPage(int page) {
  switch (page) {
  case 1: refreshPeople(); break;
  case 2: refreshBattle(true); break;
  case 3: refreshWarehouse(); break;
  case 4: refreshEffects(); break;
  case 5: refreshVariables(); break;
  case 6: refreshDiy(); break;
  case 7: refreshFatal(); break;
  default: break;
  }
}


void Win32Business::loadDiy() {
  auto &diy = window_.diy();
  if (window_.diyPath().empty()) {
    window_.clearControl(L"diy_input_4");
    window_.clearControl(L"listview_diy");
    return;
  }
  diy.load(window_.diyPath());
  window_.clearControl(L"diy_input_4");
  const auto sections = diy.sectionNames();
  for (const auto &section : sections)
    window_.addCombo(L"diy_input_4", fromUtf8(section), 0);
  if (!sections.empty()) {
    window_.setComboIndex(L"diy_input_4", 0);
    window_.diySection() = 0;
  }
  refreshDiy();
}

void Win32Business::refreshDiy() {
  auto session = this->session();
  auto &diy = window_.diy();
  if (!session || window_.diyPath().empty() || diy.sectionCount() == 0) {
    window_.clearControl(L"listview_diy");
    return;
  }
  window_.diyVisible() =
      diy.expand(window_.diySection(), window_.diyOffset(), 1000);
  window_.clearControl(L"listview_diy");
  const auto profile = this->profile();
  for (const auto &item : window_.diyVisible()) {
    std::wstring explanation;
    uint32_t value = 0;
    try {
      value = session->u(item.address, item.width, false);
      if (item.kind == "战场编号") {
        const auto total = profile.limit("battle_ours", 20) +
                           profile.limit("battle_friends", 40) +
                           profile.limit("battle_enemies", 190);
        if (value < total) {
          const auto unit = BattleService(session, profile).read(size_t(value));
          explanation = unit.dataId == 0xffff
                            ? L"空"
                            : nativePersonNameImpl(session, profile,
                                                   unit.dataId);
        } else {
          explanation = L"无效编号";
        }
      } else if (item.kind == "Data编号") {
        explanation = value < profile.limit("person_count", 1024)
                          ? nativePersonNameImpl(session, profile, uint16_t(value))
                          : L"无效编号";
      } else if (item.kind == "宝物") {
        explanation = value < profile.limit("item_count", 255)
                          ? nativeItemName(uint16_t(value))
                          : L"无效编号";
      }
    } catch (const std::exception &error) {
      explanation = L"不可读：" + fromUtf8(error.what());
    }
    const auto text = fromUtf8(item.label) + L"  " + std::to_wstring(value) +
                      L"  " + explanation + L"  " + hexAddress(item.address) +
                      L"  " + std::to_wstring(item.width) + L"  " +
                      fromUtf8(item.explanation);
    window_.addList(L"listview_diy", text, item.address);
  }
}

void Win32Business::saveDiyValue() {
  auto session = this->session();
  const int row = window_.listSelection(L"listview_diy");
  if (!session || row < 0)
    throw Error("未选择自定义变量");
  const auto &items = window_.diyVisible();
  if (size_t(row) >= items.size())
    throw Error("自定义行号越界");
  const auto &item = items[size_t(row)];
  const auto value =
      uint32_t(window_.number(L"diy_input_2", UINT32_MAX, "自定义值"));
  session->writeU(item.address, value, item.width, false);
  window_.setText(L"diy_input_2", value);
  window_.setStatus(L"自定义变量已写入");
  refreshDiy();
}

void Win32Business::openDiy() {
  wchar_t path[MAX_PATH] = {};
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = window_.handle();
  dialog.lpstrFile = path;
  dialog.nMaxFile = MAX_PATH;
  dialog.lpstrFilter = L"CSV 文件\0*.csv\0所有文件\0*.*\0\0";
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameW(&dialog))
    return;
  window_.diyPath() = path;
  loadDiy();
  window_.setStatus(L"已打开 " + std::wstring(path));
}

void Win32Business::saveDiy() {
  if (window_.diyPath().empty())
    throw Error("尚未加载 DIY.csv");
  window_.diy().save(window_.diyPath());
  window_.setStatus(L"DIY.csv 已保存");
}

void Win32Business::advanceDiyWindow() {
  if (window_.diyVisible().size() < 1000)
    return;
  window_.diyOffset() += window_.diyVisible().size();
  refreshDiy();
}

}  // namespace banshou
