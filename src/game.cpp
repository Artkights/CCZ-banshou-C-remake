#include "game.hpp"
#include <map>
namespace banshou {
static uint32_t addr(const EngineProfile &p, const char *k, uint32_t fallback) {
  return p.address(k, fallback);
}
PersonRecord PersonService::read(uint16_t id) const {
  if (id >= profile_.limit("person_count", 1024))
    throw Error("人物编号越界");
  auto pointer = session_->u(addr(profile_, "person_pointer", 0x004CEA00), 4);
  auto row = session_->readActual(
      checkedAddress(pointer, uint64_t(id) * 0x48, 0x48), 0x48);
  PersonRecord r;
  r.id = id;
  r.r = uint16_t(session_->u(
      profile_.address("person_r", 0x0050F800) + uint32_t(id) * 2, 2));
  r.s = uint16_t(session_->u(
      profile_.address("person_s", 0x00501000) + uint32_t(id) * 2, 2));
  r.face = uint16_t(session_->u(
      profile_.address("person_face", 0x0050F000) + uint32_t(id) * 2, 2));
  auto expanded = profile_.is66() || profile_.version == EngineVersion::V64 ||
                  profile_.version == EngineVersion::V65 ||
                  profile_.version == EngineVersion::V63MP;
  r.hp = uint32_t(number(row, expanded ? 0x1b : 0x1c, 4));
  r.mp = uint16_t(number(row, expanded ? 0x1f : 0x20, expanded ? 2 : 1));
  r.strength = uint16_t(number(row, 0x11, 2));
  r.defense = uint16_t(number(row, 0x13, 2));
  r.intelligence = uint16_t(number(row, 0x15, 2));
  r.agility = uint16_t(number(row, 0x17, 2));
  r.luck = uint16_t(number(row, 0x19, 2));
  for (size_t i = 0; i < r.baseStats.size(); ++i)
    r.baseStats[i] = row[0x21 + i];
  r.appearance = row[0x27];
  r.retreat = row[0x29];
  r.ours = row[0x2a];
  r.level = row[0x2c];
  r.exp = row[0x2d];
  r.job = row[0x2b];
  r.weapon = row[0x2e];
  r.weaponLevel = row[0x2f];
  r.weaponExp = row[0x30];
  r.armor = row[0x31];
  r.armorLevel = row[0x32];
  r.armorExp = row[0x33];
  r.accessory = row[0x34];
  r.field28 = row[0x28];
  r.field06 = uint16_t(number(row, 0x06, 2));
  r.field44 = uint16_t(number(row, 0x44, 2));
  r.field46 = uint16_t(number(row, 0x46, 2));
  r.kills = uint16_t(number(row, 0x35, 2));
  if (id <= profile_.limit("merit_count", 102) - 1) {
    auto merit = profile_.address("merit", 0x00508000);
    for (size_t i = 0; i < 5; ++i)
      r.merits[i] = uint16_t(session_->u(merit + uint32_t(id * 10 + i * 2), 2));
  }
  return r;
}
void PersonService::write(uint16_t id, const PersonRecord &v) {
  if (id >= profile_.limit("person_count", 1024))
    throw Error("人物编号越界");
  auto pointer = session_->u(addr(profile_, "person_pointer", 0x004CEA00), 4);
  auto at = checkedAddress(pointer, uint64_t(id) * 0x48, 0x48);
  auto expected = session_->readActual(at, 0x48);
  auto replacement = expected;
  auto expanded = profile_.is66() || profile_.version == EngineVersion::V64 ||
                  profile_.version == EngineVersion::V65 ||
                  profile_.version == EngineVersion::V63MP;
  auto put = [&](size_t offset, size_t width, uint64_t value) {
    if (offset > replacement.size() || width > replacement.size() - offset)
      throw Error("人物字段超出记录范围");
    putNumber(replacement, offset, width, value);
  };
  put(expanded ? 0x1b : 0x1c, 4, v.hp);
  put(expanded ? 0x1f : 0x20, expanded ? 2 : 1, v.mp);
  put(0x11, 2, v.strength);
  put(0x13, 2, v.defense);
  put(0x15, 2, v.intelligence);
  put(0x17, 2, v.agility);
  put(0x19, 2, v.luck);
  for (size_t i = 0; i < v.baseStats.size(); ++i)
    put(0x21 + i, 1, v.baseStats[i]);
  put(0x27, 1, v.appearance);
  put(0x29, 1, v.retreat);
  put(0x2a, 1, v.ours);
  put(0x2c, 1, v.level);
  put(0x2d, 1, v.exp);
  put(0x2b, 1, v.job);
  put(0x2e, 1, v.weapon);
  put(0x2f, 1, v.weaponLevel);
  put(0x30, 1, v.weaponExp);
  put(0x31, 1, v.armor);
  put(0x32, 1, v.armorLevel);
  put(0x33, 1, v.armorExp);
  put(0x34, 1, v.accessory);
  put(0x28, 1, v.field28);
  put(0x06, 2, v.field06);
  put(0x44, 2, v.field44);
  put(0x46, 2, v.field46);
  put(0x35, 2, v.kills);
  std::vector<CompareWriteEntry> entries;
  entries.push_back({at, expected, replacement, true, false});
  auto addWord = [&](uint32_t address, size_t width, uint64_t value) {
    auto before = session_->read(address, width);
    Bytes after(width);
    putNumber(after, 0, width, value);
    entries.push_back({address, before, after, false, false});
  };
  addWord(profile_.address("person_r", 0x0050F800) + uint32_t(id) * 2, 2, v.r);
  addWord(profile_.address("person_s", 0x00501000) + uint32_t(id) * 2, 2, v.s);
  addWord(profile_.address("person_face", 0x0050F000) + uint32_t(id) * 2, 2,
          v.face);
  if (id <= profile_.limit("merit_count", 102) - 1) {
    auto merit = profile_.address("merit", 0x00508000);
    for (size_t i = 0; i < 5; ++i)
      addWord(merit + uint32_t(id * 10 + i * 2), 2, v.merits[i]);
  }
  session_->compareExchangeTransaction(entries);
}
std::vector<PersonRecord> PersonService::readRange(size_t start,
                                                   size_t count) const {
  if (start > 1024 || count > 1024 - start)
    throw Error("人物范围越界");
  std::vector<PersonRecord> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i)
    out.push_back(read(uint16_t(start + i)));
  return out;
}
BattleRecord BattleService::read(size_t slot) const {
  auto stride = profile_.limit("battle_stride", profile_.is66() ? 0x30 : 0x24);
  auto base = profile_.address("battle_units", 0x004A7B20);
  auto count = uint64_t(profile_.limit("battle_ours", 20)) +
               profile_.limit("battle_friends", 40) +
               profile_.limit("battle_enemies", 190);
  if (slot >= count || stride < 0x24)
    throw Error("战场槽位越界");
  auto at = checkedAddress(base, uint64_t(slot) * stride, stride);
  auto row = session_->read(at, stride);
  BattleRecord r;
  r.dataId = uint16_t(number(row, 0, 2));
  r.displaySlot = row[4];
  r.faction = row[5];
  r.x = row[6];
  r.y = row[7];
  r.targetSlot = row[8];
  r.strategyX = row[9];
  r.strategyY = row[0xa];
  r.visibility = row[0xc];
  r.action = row[0xd];
  r.ai = row[0xe];
  r.direction = row[0xf];
  r.hp = uint32_t(number(row, 0x10, 4));
  r.mp = uint32_t(number(row, 0x14, 4));
  if (r.dataId != 0xffff) {
    auto sp =
        checkedAddress(profile_.address("battle_sp", 0x00501C00), r.dataId, 1);
    r.sp = uint8_t(session_->u(sp, 1));
  }
  for (size_t i = 0; i < 6 && 0x18 + i < row.size(); ++i)
    r.buffs[i] = row[0x18 + i];
  r.health = row[0x1e];
  if (row.size() > 0x23) {
    r.targetX = row[0x20];
    r.targetY = row[0x21];
    r.field22 = row[0x22];
    r.field23 = row[0x23];
  }
  for (size_t i = 0; i < 6 && 0x24 + i * 2 < row.size(); ++i)
    r.expanded[i] = row[0x24 + i * 2];
  return r;
}
static std::vector<CompareWriteEntry>
battleRowEntries(MemorySession &session, const EngineProfile &profile,
                 size_t slot, const BattleRecord &r) {
  auto stride = profile.limit("battle_stride", profile.is66() ? 0x30 : 0x24);
  auto base = profile.address("battle_units", 0x004A7B20);
  auto count = uint64_t(profile.limit("battle_ours", 20)) +
               profile.limit("battle_friends", 40) +
               profile.limit("battle_enemies", 190);
  if (slot >= count || stride < 0x24)
    throw Error("战场槽位越界");
  auto at = checkedAddress(base, uint64_t(slot) * stride, stride);
  auto expected = session.read(at, stride);
  auto replacement = expected;
  auto put = [&](size_t o, size_t w, uint64_t v) {
    if (o > stride || w > stride - o)
      throw Error("战场字段超出记录范围");
    putNumber(replacement, o, w, v);
  };
  auto save = profile.address("save_state", 0x004B0770);
  if ((session.u(checkedAddress(save, 6, 1), 1) & 1) == 0) {
    put(0x10, 4, r.hp);
    put(0x14, 4, r.mp);
  } else {
    put(0, 2, r.dataId);
    put(4, 1, r.displaySlot);
    put(5, 1, r.faction);
    put(6, 1, r.x);
    put(7, 1, r.y);
    put(8, 1, r.targetSlot);
    put(9, 1, r.strategyX);
    put(0xa, 1, r.strategyY);
    put(0xc, 1, r.visibility);
    put(0xd, 1, r.action);
    put(0xe, 1, r.ai);
    put(0xf, 1, r.direction);
    put(0x10, 4, r.hp);
    put(0x14, 4, r.mp);
    for (size_t i = 0; i < 6 && 0x18 + i < stride; ++i)
      put(0x18 + i, 1, r.buffs[i]);
    put(0x1e, 1, r.health);
    if (stride > 0x23) {
      put(0x20, 1, r.targetX);
      put(0x21, 1, r.targetY);
      put(0x22, 1, r.field22);
      put(0x23, 1, r.field23);
    }
    for (size_t i = 0; i < 6 && 0x24 + i * 2 < stride; ++i)
      put(0x24 + i * 2, 1, r.expanded[i]);
  }
  std::vector<CompareWriteEntry> entries;
  entries.push_back({at, expected, replacement, false, false});
  if (r.dataId != 0xffff) {
    auto sp =
        checkedAddress(profile.address("battle_sp", 0x00501C00), r.dataId, 1);
    auto before = session.read(sp, 1);
    Bytes after{uint8_t(r.sp)};
    entries.push_back({sp, before, after, false, false});
  }
  return entries;
}
void BattleService::write(size_t slot, const BattleRecord &value) {
  session_->compareExchangeTransaction(
      battleRowEntries(*session_, profile_, slot, value));
}
void BattleService::writeRows(
    const std::vector<std::pair<size_t, BattleRecord>> &rows) {
  if (rows.empty())
    return;
  std::vector<CompareWriteEntry> entries;
  for (const auto &row : rows) {
    auto single = battleRowEntries(*session_, profile_, row.first, row.second);
    entries.insert(entries.end(), single.begin(), single.end());
  }
  session_->compareExchangeTransaction(entries);
}
void BattleService::writePage(size_t slot, const BattleRecord &value,
                              const BattleState &state) {
  auto entries = battleRowEntries(*session_, profile_, slot, value);
  auto stateAddress = profile_.address("battle_state", 0x004B3D08);
  auto saveAddress = profile_.address("save_state", 0x004B0770);
  auto add = [&](uint32_t address, uint64_t value, size_t width) {
    auto before = session_->read(address, width);
    Bytes after(width);
    putNumber(after, 0, width, value);
    entries.push_back({address, before, after, false, false});
  };
  add(stateAddress + 3, state.turnLimit, 1);
  add(stateAddress + 2, state.currentTurn ? state.currentTurn - 1 : 0, 1);
  auto scenario = uint8_t(session_->u(saveAddress + 6, 1));
  if ((scenario & 1) && state.nextScenario <= 128)
    add(saveAddress + 6, uint8_t(state.nextScenario * 2 - 1), 1);
  static const uint8_t weather[] = {0, 3, 4, 5, 0x16};
  if (state.weather < 5)
    add(stateAddress + 4, weather[state.weather], 1);
  add(stateAddress + 0x11, state.season, 1);
  session_->compareExchangeTransaction(entries);
}
BattleState BattleStateService::read() const {
  auto save = profile_.address("save_state", 0x004B0770),
       state = profile_.address("battle_state", 0x004B3D08);
  BattleState b;
  b.turnLimit = uint8_t(session_->u(state + 3, 1));
  b.currentTurn = uint8_t(session_->u(state + 2, 1) + 1);
  auto scenario = uint8_t(session_->u(save + 6, 1));
  b.nextScenario = uint8_t(scenario / 2 + 1);
  b.season = uint8_t(session_->u(state + 0x11, 1));
  auto raw = uint8_t(session_->u(state + 4, 1));
  static const uint8_t weatherList[] = {0, 0, 0, 1, 2, 3, 0, 0, 0, 0,
                                        1, 2, 0, 1, 2, 2, 3, 3, 0, 0,
                                        1, 1, 4, 4, 0, 1, 4, 4, 4, 4};
  auto index = (size_t(b.season) * 6u + raw) % 30u;
  b.weather = weatherList[index];
  return b;
}
void BattleStateService::write(const BattleState &b) {
  auto save = profile_.address("save_state", 0x004B0770),
       state = profile_.address("battle_state", 0x004B3D08);
  std::vector<CompareWriteEntry> entries;
  auto add = [&](uint32_t address, uint64_t value, size_t width) {
    auto before = session_->read(address, width);
    Bytes after(width);
    putNumber(after, 0, width, value);
    entries.push_back({address, before, after, false, false});
  };
  add(state + 3, b.turnLimit, 1);
  add(state + 2, b.currentTurn ? b.currentTurn - 1 : 0, 1);
  auto scenario = uint8_t(session_->u(save + 6, 1));
  if (scenario & 1 && b.nextScenario <= 128)
    add(save + 6, uint8_t(b.nextScenario * 2 - 1), 1);
  static const uint8_t weather[] = {0, 3, 4, 5, 0x16};
  if (b.weather < 5)
    add(state + 4, weather[b.weather], 1);
  add(state + 0x11, b.season, 1);
  session_->compareExchangeTransaction(entries);
}
void BattleStateService::applyGroupBuff(size_t first, size_t last, size_t buff,
                                        int value) {
  if (buff >= 6 || value < 0 || value > 6)
    throw Error("状态或数值越界");
  auto stride = profile_.limit("battle_stride", profile_.is66() ? 0x30 : 0x24),
       base = profile_.address("battle_units", 0x004A7B20);
  auto count = uint64_t(profile_.limit("battle_ours", 20)) +
               profile_.limit("battle_friends", 40) +
               profile_.limit("battle_enemies", 190);
  if (first > last || last > count)
    throw Error("批量状态范围越界");
  auto encoded = value == 0 ? uint8_t(0) : uint8_t(value);
  for (size_t i = first; i < last; ++i)
    session_->writeU(
        checkedAddress(base, uint64_t(i) * stride + 0x18 + buff, 1), encoded, 1,
        false);
}
uint8_t WarehouseService::read(size_t index) const {
  if (index >= 200)
    throw Error("仓库索引越界");
  return uint8_t(session_->u(profile_.address("warehouse", 0x004B0783) +
                                 uint32_t(index) * 3,
                             1, false));
}
WarehouseRecord WarehouseService::readRecord(size_t index) const {
  if (index >= 200)
    throw Error("仓库索引越界");
  auto a = profile_.address("warehouse", 0x004B0783) + uint32_t(index) * 3;
  return {uint8_t(session_->u(a, 1)), uint8_t(session_->u(a + 1, 1)),
          uint8_t(session_->u(a + 2, 1))};
}
std::vector<WarehouseRecord> WarehouseService::readRecords(size_t count) const {
  if (count > 200)
    count = 200;
  auto base = profile_.address("warehouse", 0x004B0783);
  std::vector<WarehouseRecord> records;
  records.reserve(count);
  // 加壳或分页受保护的基底可能拒绝一次读 600 字节，此时退回逐条读取。
  try {
    auto raw = session_->read(base, count * 3);
    if (raw.size() != count * 3)
      throw Error("仓库批量读取长度不符");
    for (size_t i = 0; i < count; ++i)
      records.push_back({raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]});
    return records;
  } catch (...) {
    for (size_t i = 0; i < count; ++i)
      records.push_back(readRecord(i));
    return records;
  }
}
void WarehouseService::write(size_t index, uint8_t value) {
  if (index >= 200)
    throw Error("仓库索引越界");
  auto address = profile_.address("warehouse", 0x004B0783) +
                 uint32_t(index) * 3;
  session_->compareExchangeTransaction(
      {{address, session_->read(address, 1), Bytes{value}, false, false}});
}
void WarehouseService::writeRecord(size_t index, const WarehouseRecord &v) {
  if (index >= 200)
    throw Error("仓库索引越界");
  auto a = profile_.address("warehouse", 0x004B0783) + uint32_t(index) * 3;
  auto expected = session_->read(a, 3);
  Bytes replacement{uint8_t(v.item), uint8_t(v.level), uint8_t(v.experience)};
  session_->compareExchangeTransaction(
      {{a, expected, replacement, false, false}});
}
void WarehouseService::writePage(const WarehousePageEdit &edit) {
  std::vector<CompareWriteEntry> entries;
  if (edit.writeRecord) {
    if (edit.index >= 200)
      throw Error("仓库索引越界");
    auto address = profile_.address("warehouse", 0x004B0783) +
                   uint32_t(edit.index) * 3;
    auto expected = session_->read(address, 3);
    Bytes replacement{uint8_t(edit.record.item),
                      uint8_t(edit.record.level),
                      uint8_t(edit.record.experience)};
    entries.push_back({address, expected, replacement, false, false});
  }
  if (edit.writeConsumable) {
    auto address = consumableAddress(edit.consumableIndex);
    entries.push_back({address, session_->read(address, 1),
                       Bytes{edit.consumableValue}, false, false});
  }
  if (edit.writeGlobals) {
    auto add = [&](const char *key, uint32_t fallback, uint64_t value,
                   size_t width) {
      auto address = profile_.address(key, fallback);
      auto before = session_->read(address, width);
      Bytes after(width);
      putNumber(after, 0, width, value);
      entries.push_back({address, before, after, false, false});
    };
    add("money", 0x004B077C, edit.money, 4);
    add("alignment", 0x004B0782, edit.alignment, 1);
    add("merit_pool", 0x00505F40, edit.meritPool, 4);
  }
  if (!entries.empty())
    session_->compareExchangeTransaction(entries);
}
void WarehouseService::clearWarehouse() {
  auto count = profile_.limit("warehouse_count", 200);
  auto base = profile_.address("warehouse", 0x004B0783);
  std::vector<CompareWriteEntry> entries;
  for (uint32_t i = 0; i < count; ++i) {
    auto address = checkedAddress(base, uint64_t(i) * 3, 1);
    entries.push_back({address, session_->read(address, 1), Bytes{0xff},
                       false, false});
  }
  if (!entries.empty())
    session_->compareExchangeTransaction(entries);
}
size_t WarehouseService::consumableCount() const {
  if (profile_.consumables.valid())
    return profile_.consumables.storageCount;
  return profile_.limit("consumable_storage_count", 0);
}
uint16_t WarehouseService::consumableItemId(size_t index) const {
  if (index >= consumableCount())
    throw Error("消耗品索引越界");
  if (profile_.consumables.valid() &&
      index < profile_.consumables.itemIds.size())
    return profile_.consumables.itemIds[index];
  if (index < profile_.consumableItemIds.size())
    return profile_.consumableItemIds[index];
  return uint16_t(profile_.limit("consumable_id_min", 0) + index);
}
uint32_t WarehouseService::consumableAddress(size_t index) const {
  if (index >= consumableCount())
    throw Error("消耗品索引越界");
  const auto &spec = profile_.consumables;
  if (spec.valid()) {
    if (!spec.addresses.empty()) {
      if (index >= spec.addresses.size())
        throw Error("消耗品显式地址索引越界");
      return spec.addresses[index];
    }
    if (spec.baseDereference)
      return checkedAddress(
          uint32_t(session_->u(spec.base, 4, false)), index * spec.stride,
          spec.width);
    return checkedAddress(spec.base, uint64_t(index) * spec.stride,
                          spec.width);
  }
  throw Error("当前画像没有可用的消耗品映射");
}
uint8_t WarehouseService::readConsumable(size_t index) const {
  return uint8_t(session_->u(consumableAddress(index), 1, false));
}
void WarehouseService::writeConsumable(size_t index, uint8_t value) {
  auto address = consumableAddress(index);
  session_->compareExchangeTransaction(
      {{address, session_->read(address, 1), Bytes{value}, false, false}});
}
void WarehouseService::setAllConsumables(uint8_t value) {
  std::vector<CompareWriteEntry> entries;
  for (size_t i = 0; i < consumableCount(); ++i) {
    auto address = consumableAddress(i);
    entries.push_back({address, session_->read(address, 1), Bytes{value},
                       false, false});
  }
  if (!entries.empty())
    session_->compareExchangeTransaction(entries);
}
void WarehouseService::clearConsumables() { setAllConsumables(0); }
size_t WarehouseService::addAllTreasures(uint8_t level, uint8_t experience) {
  level = std::min<uint8_t>(level, 9);
  auto equipment = profile_.limit("equipment_count", 0);
  auto table = profile_.address("item_table", 0);
  if (!equipment || !table)
    throw Error("当前画像尚未解析宝物表，不能执行一键全宝");
  std::vector<uint8_t> treasures;
  for (uint32_t id = 0; id < equipment; ++id) {
    if ((profile_.version == EngineVersion::V61 ||
         profile_.version == EngineVersion::V62 ||
         profile_.version == EngineVersion::V63 ||
         profile_.version == EngineVersion::V63MP) &&
        id >= 87 && id < 130)
      continue;
    const bool flag = profile_.is66()
                          ? session_->u(checkedAddress(
                                            table, uint64_t(id) * 25 + 0x18, 1),
                                        1, false) == 1
                          : session_->u(checkedAddress(
                                            table, uint64_t(id) * 25 + 0x14, 1),
                                        1, false) != 0xff;
    if (flag)
      treasures.push_back(uint8_t(id));
  }
  size_t inserted = 0;
  for (size_t slot = 0; slot < profile_.limit("warehouse_count", 200) &&
                        inserted < treasures.size();
       ++slot) {
    if (readRecord(slot).item != 0xff)
      continue;
    auto itemExperience =
        experience == 0xff && level != 9 ? uint8_t(250) : experience;
    writeRecord(slot, {treasures[inserted++], level, itemExperience});
  }
  return inserted;
}
const VariableLayout &VariableService::layout(VariableKind kind) const {
  return kind == VariableKind::Boolean
             ? profile_.boolVariables
             : kind == VariableKind::Integer ? profile_.intVariables
                                             : profile_.ptrVariables;
}
size_t VariableService::recordCount(VariableKind kind) const {
  return layout(kind).recordCount;
}
size_t VariableService::browseCount(VariableKind kind) const {
  return layout(kind).browseCount;
}
size_t VariableService::writableCount(VariableKind kind) const {
  return layout(kind).recordCount;
}
bool VariableService::readBool(size_t index) const {
  const auto &l = layout(VariableKind::Boolean);
  if (index >= l.browseCount)
    throw Error("布尔编号必须在 0.." + std::to_string(l.browseCount - 1));
  if (l.storage == VariableStorage::Bitset) {
    auto address = checkedAddress(l.base, index / 8, 1);
    auto byte = session_->u(address, 1, false);
    return (byte & (l.msb0 ? (0x80u >> (index % 8)) : (1u << (index % 8)))) != 0;
  }
  auto address = checkedAddress(l.base, uint64_t(index) * l.stride, l.width);
  return session_->u(address, l.width, false) != 0;
}
void VariableService::writeBool(size_t index, bool value) {
  const auto &l = layout(VariableKind::Boolean);
  if (index >= l.recordCount)
    throw Error("布尔编号必须在 0.." + std::to_string(l.recordCount - 1));
  if (l.storage == VariableStorage::Bitset) {
    auto address = checkedAddress(l.base, index / 8, 1);
    auto byte = session_->u(address, 1, false);
    const auto mask = l.msb0 ? (0x80u >> (index % 8)) : (1u << (index % 8));
    auto next = value ? uint8_t(byte | mask) : uint8_t(byte & ~mask);
    session_->writeU(address, next, 1, false);
    return;
  }
  session_->writeU(checkedAddress(l.base, uint64_t(index) * l.stride, l.width),
                   value ? 1 : 0, l.width, false);
}
void VariableService::writeBools(
    const std::vector<std::pair<size_t, bool>> &values) {
  const auto &l = layout(VariableKind::Boolean);
  std::vector<CompareWriteEntry> entries;
  if (l.storage == VariableStorage::Bitset) {
    std::map<uint32_t, std::pair<uint8_t, uint8_t>> groups;
    for (const auto &value : values) {
      if (value.first >= l.recordCount)
        throw Error("布尔编号越界: " + std::to_string(value.first));
      auto address = checkedAddress(l.base, value.first / 8, 1);
      auto it = groups.find(address);
      if (it == groups.end()) {
        auto current = uint8_t(session_->u(address, 1, false));
        it = groups.emplace(address, std::make_pair(current, current)).first;
      }
      const auto mask =
          l.msb0 ? (0x80u >> (value.first % 8)) : (1u << (value.first % 8));
      if (value.second)
        it->second.second = uint8_t(it->second.second | mask);
      else
        it->second.second = uint8_t(it->second.second & ~mask);
    }
    for (const auto &group : groups)
      entries.push_back({group.first, Bytes{group.second.first},
                         Bytes{group.second.second}, false, false});
  } else {
    for (const auto &value : values) {
      if (value.first >= l.recordCount)
        throw Error("布尔编号越界: " + std::to_string(value.first));
      auto address =
          checkedAddress(l.base, uint64_t(value.first) * l.stride, l.width);
      auto expected = session_->read(address, l.width);
      Bytes replacement(l.width);
      putNumber(replacement, 0, l.width, value.second ? 1 : 0);
      entries.push_back({address, expected, replacement, false, false});
    }
  }
  if (!entries.empty())
    session_->compareExchangeTransaction(entries);
}
int32_t VariableService::readInt(size_t index) const {
  const auto &l = layout(VariableKind::Integer);
  if (index >= l.recordCount)
    throw Error("变量索引越界");
  return int32_t(session_->u(
      checkedAddress(l.base, uint64_t(index) * l.stride, l.width), l.width,
      false));
}
void VariableService::writeInt(size_t index, int32_t value) {
  const auto &l = layout(VariableKind::Integer);
  if (index >= l.recordCount)
    throw Error("变量索引越界");
  session_->writeU(
      checkedAddress(l.base, uint64_t(index) * l.stride, l.width),
      uint32_t(value), l.width, false);
}
uint32_t VariableService::readPointer(size_t index) const {
  const auto &l = layout(VariableKind::Pointer);
  if (index >= l.recordCount)
    throw Error("指针变量索引越界");
  return session_->u(
      checkedAddress(l.base, uint64_t(index) * l.stride, l.width), l.width,
      false);
}
void VariableService::writePointer(size_t index, uint32_t value) {
  const auto &l = layout(VariableKind::Pointer);
  if (index >= l.recordCount)
    throw Error("指针变量索引越界");
  session_->writeU(
      checkedAddress(l.base, uint64_t(index) * l.stride, l.width), value,
      l.width, false);
}
std::vector<VariableCell>
VariableService::readPage(VariableKind kind, size_t start, size_t count) const {
  const auto &l = layout(kind);
  if (start > l.browseCount || count > l.browseCount - start)
    throw Error("变量分页范围越界");
  std::vector<VariableCell> out;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    auto index = start + i;
    VariableCell cell;
    cell.index = index;
    if (kind == VariableKind::Boolean &&
        l.storage == VariableStorage::Bitset) {
      cell.address = checkedAddress(l.base, index / 8, 1);
      auto byte = session_->u(cell.address, 1);
      const auto mask = l.msb0 ? (0x80u >> (index % 8))
                               : (1u << (index % 8));
      cell.value = (byte & mask) ? 1 : 0;
    } else {
      cell.address =
          checkedAddress(l.base, uint64_t(index) * l.stride, l.width);
      cell.value = session_->u(cell.address, l.width);
    }
    out.push_back(cell);
  }
  return out;
}
void PatchManager::apply(uint32_t address, const Bytes &expected,
                         const Bytes &replacement) {
  session_->compareExchangeTransaction(
      {{address, expected, replacement, true, true}});
}
void PatchManager::restore(uint32_t address, const Bytes &expected,
                           const Bytes &original) {
  session_->compareExchangeTransaction(
      {{address, expected, original, true, true}});
}
Bytes LegacyAutoReturnPatch::buildStub(const Bytes &original,
                                       const EngineProfile &profile) {
  if (original.size() < 0x49)
    throw Error("自动回归代码洞不足 0x49 字节");
  Bytes out = original;
  auto overlay = [&](size_t offset, uint64_t value, size_t width = 4) {
    putNumber(out, offset, width, value);
  };
  overlay(0x00, 0x83EC8B55);
  overlay(0x04, 0x75FF04EC);
  bool oldest = profile.version == EngineVersion::V62 ||
                profile.version == EngineVersion::V61;
  overlay(0x08, oldest ? 0x9E8DE808 : 0xCF44E808);
  overlay(0x0C, oldest ? 0x4D8B0004 : 0x4D8BFFF8);
  overlay(0x10, 0xFFE18108);
  overlay(0x17, 0xC96B, 2);
  overlay(0x19, profile.limit("battle_stride", 0x30));
  overlay(0x1A, 0x81, 1);
  overlay(0x1B, 0xC1, 1);
  overlay(0x1C, profile.address("battle_units", 0x004A7B20), 3);
  overlay(0x20, 0xB8FC4D89);
  overlay(0x24, 0x06, 1);
  overlay(0x28, 0x4120D0F7);
  overlay(0x2C, 0x0D, 1);
  bool legacyCall = profile.version == EngineVersion::V63MP ||
                    profile.version == EngineVersion::V63 || oldest;
  if (legacyCall) {
    overlay(0x2D, 0xE831FF, 3);
    overlay(0x30, 0xFFFB86A2);
    overlay(0x34, 0x00008868);
    overlay(0x38, 0x50066A00);
  } else {
    overlay(0x2D, 0x00008868);
    overlay(0x31, 0x8B066A00);
    overlay(0x35, 0x71FFFC4D);
    overlay(0x39, 0x00909004, 3);
  }
  overlay(0x3C, 0xE8, 1);
  overlay(0x3D, 0xFFFB8869);
  overlay(0x41, 0xFE5DE58B);
  overlay(0x45, 0x00C3FC4D);
  return out;
}
void LegacyAutoReturnPatch::set(const std::shared_ptr<MemorySession> &session,
                                const EngineProfile &profile, bool enabled) {
  if (!session)
    throw Error("尚未附加游戏");
  if (profile.version != EngineVersion::V66)
    throw Error("当前画像没有自动回归配方");
  if (session->imagePath.empty())
    throw Error("缺少目标主模块路径，无法定位自动回归分支");
  PEImage image(session->imagePath);
  const auto catalog = resourceJson(103);
  const auto patterns = catalog.contains("patterns")
                            ? catalog["patterns"]
                            : Json::object();
  const auto pattern = [&](const char *key) {
    if (!patterns.contains(key))
      throw Error(std::string("缺少自动回归签名: ") + key);
    return patterns[key].get<std::string>();
  };
  const auto unique = [&](const char *key) {
    auto hits = image.findSignature(MaskedSignature::parse(pattern(key)), true);
    if (hits.size() != 1)
      throw Error(std::string("自动回归") + key + "签名匹配数为" +
                  std::to_string(hits.size()));
    return hits.front();
  };
  const auto gate = unique("AUTO_RETURN_GATE_PATTERN_66");
  const auto state = unique("AUTO_RETURN_STATE_PATTERN_66");
  const auto effect = unique("AUTO_RETURN_EFFECT_PATTERN_66");
  const auto callTarget = [&](uint32_t address) {
    auto bytes = image.readVA(address, 5);
    if (bytes[0] != 0xE8)
      throw Error("自动回归调用点不是 CALL rel32");
    const auto relative = int32_t(number(bytes, 1, 4));
    return uint32_t(int64_t(address) + 5 + relative);
  };
  if (callTarget(gate + 4) != effect || callTarget(state) != effect)
    throw Error("自动回归调用点未共同指向原生二次行动函数");
  struct Site {
    uint32_t reference;
    std::array<uint8_t, 2> original;
  };
  const std::array<Site, 3> sites = {{
      {gate + 0x0B, {0x74, 0x13}},
      {state + 0x07, {0x74, 0x02}},
      {state + 0x1E, {0x74, 0x02}},
  }};
  const Bytes patched{0x90, 0x90};
  PatchManager manager(session);
  for (const auto &site : sites) {
    const Bytes original(site.original.begin(), site.original.end());
    auto actual = session->actual(site.reference);
    auto current = session->readActual(actual, 2);
    if (current == original) {
      if (enabled)
        manager.apply(actual, original, patched);
    } else if (current == patched) {
      if (!enabled)
        manager.restore(actual, patched, original);
    } else {
      throw Error("自动回归站点原字节不匹配");
    }
  }
}
uint32_t EffectService::table() const {
  auto pointer = profile_.address("effect_assign_pointer", 0);
  if (pointer)
    return session_->u(pointer, 4, false);
  return profile_.address("effect_assign", 0);
}
EffectRecord EffectService::read(size_t id) const {
  auto count = profile_.limit("effect_record_count",
                              profile_.limit("effect_count", 255));
  if (id >= count)
    throw Error("特效编号越界");
  auto base = table();
  if (!base)
    throw Error("特效表尚未解析");
  auto stride = profile_.limit("effect_stride", profile_.is66() ? 16 : 8);
  auto raw = session_->readActual(
      checkedAddress(base, uint64_t(id) * stride, stride), stride);
  EffectRecord out{};
  if (stride >= 16) {
    for (int i = 0; i < 4; ++i) {
      out.characters[i] = uint16_t(number(raw, i * 3, 2));
      out.characterValues[i] = raw[i * 3 + 2];
    }
    for (int i = 0; i < 2; ++i) {
      out.jobs[i] = raw[12 + i * 2];
      out.jobValues[i] = raw[13 + i * 2];
    }
  } else {
    for (int i = 0; i < 3; ++i)
      out.characters[i] = uint16_t(number(raw, i * 2, 2));
    out.jobs[0] = raw[6];
    out.jobValues[0] = raw[7];
  }
  return out;
}
void EffectService::write(size_t id, const EffectRecord &v) {
  auto count = profile_.limit("effect_record_count",
                              profile_.limit("effect_count", 255));
  if (id >= count)
    throw Error("特效编号越界");
  auto base = table();
  auto stride = profile_.limit("effect_stride", profile_.is66() ? 16 : 8);
  auto at = checkedAddress(base, uint64_t(id) * stride, stride);
  auto raw = session_->readActual(at, stride);
  if (stride >= 16) {
    for (int i = 0; i < 4; ++i) {
      putNumber(raw, i * 3, 2, v.characters[i]);
      raw[i * 3 + 2] = v.characterValues[i];
    }
    for (int i = 0; i < 2; ++i) {
      raw[12 + i * 2] = v.jobs[i];
      raw[13 + i * 2] = v.jobValues[i];
    }
  } else {
    for (int i = 0; i < 3; ++i)
      putNumber(raw, i * 2, 2, v.characters[i]);
    raw[6] = v.jobs[0];
    raw[7] = v.jobValues[0];
  }
  session_->guardedCompareExchangeTransaction(
      {{at, session_->readActual(at, stride), raw, true, false}});
}
ExclusiveEffectRecord ExclusiveEffectService::read(size_t id) const {
  auto count = profile_.limit("effect_count", 255);
  if (id >= count)
    throw Error("专属/套装特效编号越界");
  auto base = profile_.address("exclusive_set", 0x0050E800);
  auto raw =
      session_->read(checkedAddress(base, uint64_t(id) * 16, 16), 16);
  ExclusiveEffectRecord out{};
  for (size_t i = 0; i < 2; ++i) {
    auto member = i * 4;
    out.persons[i] = uint16_t(number(raw, member, 2));
    out.personItems[i] = raw[member + 2];
    out.personValues[i] = raw[member + 3];
    auto set = 8 + i * 4;
    for (size_t j = 0; j < 3; ++j)
      out.setItems[i][j] = raw[set + j];
    out.setValues[i] = raw[set + 3];
  }
  return out;
}
void ExclusiveEffectService::write(size_t id,
                                   const ExclusiveEffectRecord &v) {
  auto count = profile_.limit("effect_count", 255);
  if (id >= count)
    throw Error("专属/套装特效编号越界");
  auto base = profile_.address("exclusive_set", 0x0050E800);
  auto at = checkedAddress(base, uint64_t(id) * 16, 16);
  auto expected = session_->read(at, 16);
  auto raw = expected;
  for (size_t i = 0; i < 2; ++i) {
    auto member = i * 4;
    putNumber(raw, member, 2, v.persons[i]);
    raw[member + 2] = v.personItems[i];
    raw[member + 3] = v.personValues[i];
    auto set = 8 + i * 4;
    for (size_t j = 0; j < 3; ++j)
      raw[set + j] = v.setItems[i][j];
    raw[set + 3] = v.setValues[i];
  }
  session_->guardedCompareExchangeTransaction(
      {{at, expected, raw, false, false}});
}
uint32_t FatalService::table() const {
  auto pointer = profile_.address("fatal_pointer", 0);
  return pointer ? session_->u(pointer, 4, false)
                 : profile_.address("fatal_table", 0);
}
FatalRecord FatalService::read(size_t id) const {
  if (id >= profile_.limit("fatal_count", 80))
    throw Error("必杀编号越界");
  auto raw = session_->readActual(checkedAddress(table(), id * 16, 16), 16);
  FatalRecord o{};
  for (int i = 0; i < 5; ++i) {
    o.persons[i] = uint16_t(number(raw, i * 3, 2));
    o.levels[i] = raw[i * 3 + 2];
  }
  o.value = raw[15];
  return o;
}
void FatalService::write(size_t id, const FatalRecord &v) {
  if (id >= profile_.limit("fatal_count", 80))
    throw Error("必杀编号越界");
  auto at = checkedAddress(table(), id * 16, 16);
  auto expected = session_->readActual(at, 16);
  auto raw = expected;
  for (int i = 0; i < 5; ++i) {
    putNumber(raw, i * 3, 2, v.persons[i]);
    raw[i * 3 + 2] = v.levels[i];
  }
  raw[15] = v.value;
  session_->guardedCompareExchangeTransaction(
      {{at, expected, raw, true, false}});
}
void DiyService::write(uint32_t address, uint32_t value, size_t width) {
  if (width != 1 && width != 2 && width != 4)
    throw Error("DIY 字段宽度只能是 1、2 或 4");
  Bytes b(width);
  putNumber(b, 0, width, value);
  session_->write(address, b, false);
}
} // namespace banshou
