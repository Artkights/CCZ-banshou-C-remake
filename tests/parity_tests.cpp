#define _CRT_SECURE_NO_WARNINGS
#include "diy.hpp"
#include "evidence.hpp"
#include "game.hpp"
#include "image.hpp"
#include "profile.hpp"
#include "remote.hpp"
#include "variables.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace banshou;

#ifndef BANSHOU_SOURCE_DIR
#define BANSHOU_SOURCE_DIR "."
#endif

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << __LINE__ << ": check failed: " << #condition << "\n";       \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

std::wstring sourceRoot() {
  const std::string root = BANSHOU_SOURCE_DIR;
  return wide(root);
}

std::wstring envPath(const wchar_t *name) {
  const wchar_t *value = _wgetenv(name);
  return value ? value : L"";
}

std::wstring tempDirectory(const wchar_t *name) {
  auto path = std::filesystem::temp_directory_path() / name;
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path.wstring();
}

Json readJsonFile(const std::wstring &path) {
  auto bytes = readFile(path);
  return Json::parse(std::string(bytes.begin(), bytes.end()));
}

std::shared_ptr<FakeMemoryBackend> fakeBackend(uint32_t base, size_t size) {
  auto backend = std::make_shared<FakeMemoryBackend>();
  backend->regions[base] = Bytes(size);
  return backend;
}

EngineProfile simpleProfile() {
  EngineProfile profile;
  profile.version = EngineVersion::V66;
  return profile;
}

void testMemory() {
  auto backend = fakeBackend(0x500000, 0x2000);
  MemorySession session(backend, 0x500000, 0x2000);
  session.referenceBase = 0x400000;
  session.writeU(0x400010, 0x1234, 2);
  CHECK(session.u(0x400010, 2) == 0x1234);
  CHECK(session.actual(0x400010) == 0x500010);
  CHECK(backend->regions[0x500000][0x10] == 0x34);

  bool rejected = false;
  try {
    checkedAddress(0xffffffff, 2, 4);
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);

  MemorySession writable(backend, 0x500000, 0x2000);
  writable.writeU(0x400010, 1, 1);
  CHECK(backend->regions[0x500000][0x10] == 1);

  session.writeU(0x400020, 1, 1);
  session.writeU(0x400021, 9, 1);
  backend->failWrite = 0x500021;
  rejected = false;
  try {
    session.writeTransaction(
        {{0x500020, Bytes{1}, Bytes{2}, true, false},
         {0x500021, Bytes{9}, Bytes{3}, true, false}});
  } catch (...) {
    rejected = true;
  }
  backend->failWrite = 0;
  CHECK(rejected);
  CHECK(session.u(0x500020, 1, true) == 1);
  CHECK(session.u(0x500021, 1, true) == 9);

  rejected = false;
  try {
    session.compareExchangeTransaction(
        {{0x500020, Bytes{0xEE}, Bytes{7}, true, false}});
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);
  CHECK(session.u(0x500020, 1, true) == 1);

  backend->failWrite = 0x500020;
  rejected = false;
  try {
    session.compareExchangeTransaction(
        {{0x500020, Bytes{1}, Bytes{7}, true, false}});
  } catch (...) {
    rejected = true;
  }
  backend->failWrite = 0;
  CHECK(rejected);
  CHECK(session.u(0x500020, 1, true) == 1);
}

void testProfile() {
  ProfileRegistry registry;
  CHECK(registry.legacy(0).version == EngineVersion::V64);
  CHECK(registry.legacy(1).version == EngineVersion::V63MP);
  CHECK(registry.legacy(2).version == EngineVersion::V63);
  CHECK(registry.legacy(3).version == EngineVersion::V62);
  CHECK(registry.legacy(4).version == EngineVersion::V61);
  CHECK(registry.legacy(5).version == EngineVersion::V65);
  CHECK(registry.legacy(6).version == EngineVersion::V66);
  auto legacy65 = registry.legacy(5);
  CHECK(legacy65.capabilities["battle"].state == "Enabled");
  CHECK(legacy65.capabilities["battle"].recipeId == "legacy.5.battle");
  CHECK(legacy65.capabilities["battle"].writable);
  CHECK(legacy65.limit("effect_stride") == 8);
  CHECK(legacy65.limit("item_name_size") == 17);
  CHECK(legacy65.remoteAddress("revive") == 0x004092C7);
  auto legacy66 = registry.legacy(6);
  CHECK(legacy66.consumableItemIds.size() == 352);
  CHECK(legacy66.boolVariables.storage == VariableStorage::Bitset);
  CHECK(legacy66.boolVariables.writableCount == 2048);
  CHECK(legacy66.boolVariables.browseCount == 2048);
  CHECK(legacy66.ptrVariables.recordCount == 481);
  CHECK(legacy66.ptrVariables.base == 0x00506000);
  CHECK(registry.legacy(0).limit("variable_ptr_count") == 481);
  CHECK(registry.legacy(0).boolVariables.storage == VariableStorage::Array);
  CHECK(registry.legacy(0).boolVariables.stride == 4);
  CHECK(registry.legacy(0).boolVariables.recordCount == 4096);

  const auto root = tempDirectory(L"banshou_parity_layout");
  writeFile(root + L"\\Data.e5", Bytes(35764, 0));
  writeFile(root + L"\\Star.e5", Bytes(3800, 0));
  auto layoutProfile = registry.legacy(6);
  applyItemFileLayout(layoutProfile, root + L"\\game.exe");
  CHECK(layoutProfile.limit("item_count") == 255);
  CHECK(layoutProfile.limit("item_record_count") == 256);
  writeFile(root + L"\\Star.e5", Bytes(0x27D8, 0));
  applyItemFileLayout(layoutProfile, root + L"\\game.exe");
  CHECK(layoutProfile.limit("item_count") == 512);
  layoutProfile.consumables.key = "single-exe-66-256-v1";
  writeFile(root + L"\\Star.e5", Bytes(100, 0));
  applyItemFileLayout(layoutProfile, root + L"\\game.exe");
  CHECK(layoutProfile.limit("item_count") == 512);
  std::filesystem::remove_all(root);

  auto sample65 = envPath(L"BANSHOU_SAMPLE_65");
  if (!sample65.empty() && std::filesystem::exists(std::filesystem::path(sample65))) {
    auto detected = registry.detect(sample65);
    CHECK(detected.version == EngineVersion::V65);
    CHECK(detected.consumables.storageCount == 105);
    CHECK(detected.limit("item_name_size") == 17);
    CHECK(detected.boolVariables.storage == VariableStorage::Array);
    CHECK(detected.boolVariables.recordCount == 4096);
  }
  auto sample66 = envPath(L"BANSHOU_SAMPLE_66");
  if (!sample66.empty() && std::filesystem::exists(std::filesystem::path(sample66))) {
    auto detected = registry.detect(sample66);
    CHECK(detected.version == EngineVersion::V66);
    CHECK(detected.consumables.storageCount == 352);
    CHECK(detected.consumables.idMin == 160);
    CHECK(detected.consumables.idMax == 511);
    CHECK(detected.limit("item_record_count") == 256);
    CHECK(detected.boolVariables.writableCount == 2048);
    CHECK(detected.boolVariables.browseCount == 2048);
    CHECK(detected.ptrVariables.recordCount == 481);
  }
}

void testItem() {
  EngineProfile profile;
  profile.version = EngineVersion::V64;
  profile.addresses["warehouse"] = 0x401000;
  profile.addresses["item_table"] = 0x402000;
  profile.limits["warehouse_count"] = 2;
  profile.limits["equipment_count"] = 3;
  profile.limits["item_stride"] = 25;
  auto backend = fakeBackend(0x400000, 0x4000);
  auto &region = backend->regions[0x400000];
  for (size_t index = 0; index < 200; ++index)
    region[0x1000 + index * 3] = 0xff;
  for (size_t item = 0; item < 3; ++item)
    region[0x2000 + item * 25 + 0x14] = 0x00;
  MemorySession session(backend, 0x400000, 0x4000);
  WarehouseService warehouse(std::make_shared<MemorySession>(session), profile);
  CHECK(warehouse.addAllTreasures(9, 0xff) == 2);
  CHECK(region[0x1000] == 0);
  CHECK(region[0x1001] == 9);
  CHECK(region[0x1002] == 0xff);
  CHECK(region[0x1003] == 1);
  CHECK(region[0x1006] == 0xff);

  EngineProfile split;
  split.version = EngineVersion::V61;
  split.addresses["consumable_counts"] = 0x400100;
  split.limits["consumable_storage_count"] = 43;
  split.consumables.key = "legacy-split-43-v1";
  split.consumables.base = 0x400100;
  split.consumables.idMin = 87;
  split.consumables.storageCount = 43;
  split.consumables.equipmentCount = 154;
  for (uint32_t id = 87; id < 130; ++id)
    split.consumables.itemIds.push_back(uint16_t(id));
  for (uint32_t i = 0; i < 43; ++i)
    split.consumables.addresses.push_back(i < 17 ? 0x400100 + i
                                                 : 0x402000 + (i - 17));
  auto splitSession = std::make_shared<MemorySession>(
      fakeBackend(0x400000, 0x4000), 0x400000, 0x4000);
  WarehouseService splitWarehouse(splitSession, split);
  CHECK(splitWarehouse.consumableCount() == 43);
  splitWarehouse.writeConsumable(16, 7);
  splitWarehouse.writeConsumable(17, 9);
  CHECK(splitSession->u(0x400110, 1, true) == 7);
  CHECK(splitSession->u(0x402000, 1, true) == 9);

  auto consumableBackend = fakeBackend(0x400000, 0x4000);
  EngineProfile standard;
  standard.version = EngineVersion::V66;
  standard.consumables.key = "standard-66-352-v1";
  standard.consumables.base = 0x401000;
  standard.consumables.idMin = 160;
  standard.consumables.storageCount = 352;
  standard.consumables.equipmentCount = 160;
  for (uint32_t id = 160; id <= 511; ++id)
    standard.consumables.itemIds.push_back(uint16_t(id));
  WarehouseService standardWarehouse(
      std::make_shared<MemorySession>(consumableBackend, 0x400000, 0x4000),
      standard);
  CHECK(standardWarehouse.consumableCount() == 352);
  standardWarehouse.setAllConsumables(0xff);
  CHECK(consumableBackend->regions[0x400000][0x1000] == 0xff);
  CHECK(consumableBackend->regions[0x400000][0x115f] == 0xff);
}

void testVariable() {
  EngineProfile profile;
  profile.addresses["bool_vars"] = 0x401000;
  profile.addresses["int_vars"] = 0x402000;
  profile.addresses["ptr_vars"] = 0x403000;
  profile.boolVariables.base = 0x401000;
  profile.boolVariables.storage = VariableStorage::Bitset;
  profile.boolVariables.stride = 1;
  profile.boolVariables.width = 1;
  profile.boolVariables.recordCount = 1536;
  profile.boolVariables.writableCount = 1536;
  profile.boolVariables.browseCount = 2048;
  profile.boolVariables.isSigned = false;
  profile.boolVariables.msb0 = true;
  profile.intVariables.base = 0x402000;
  profile.intVariables.recordCount = 4096;
  profile.intVariables.writableCount = 4096;
  profile.intVariables.browseCount = 4096;
  profile.ptrVariables.base = 0x403000;
  profile.ptrVariables.recordCount = 481;
  profile.ptrVariables.writableCount = 481;
  profile.ptrVariables.browseCount = 481;
  auto backend = fakeBackend(0x400000, 0x4000);
  auto session =
      std::make_shared<MemorySession>(backend, 0x400000, 0x4000);
  VariableService variables(session, profile);
  variables.writeBools({{1, true}, {2, true}, {3, false}});
  CHECK(session->u(0x401000, 1, true) == 0x60);
  variables.writeBool(2, false);
  CHECK(session->u(0x401000, 1, true) == 0x40);
  variables.writeInt(2, -7);
  CHECK(variables.readInt(2) == -7);
  variables.writePointer(480, 0x12345678);
  CHECK(variables.readPointer(480) == 0x12345678);
  bool rejected = false;
  try {
    variables.writeBool(1536, true);
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);
  rejected = false;
  try {
    variables.writeBools({{1536, true}});
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);
  rejected = false;
  try {
    variables.readPointer(481);
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);

  std::vector<bool> flags(16, false);
  flags[0] = true;
  flags[9] = true;
  auto archive = encodeBooleanArchive(flags, flags.size());
  CHECK(decodeBooleanArchive(archive, flags.size()) == flags);
  auto corrupt = archive;
  corrupt[0] = 'X';
  rejected = false;
  try {
    decodeBooleanArchive(corrupt, flags.size());
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);
}

void testTransaction() {
  EngineProfile profile;
  profile.version = EngineVersion::V64;
  profile.addresses["person_pointer"] = 0x400000;
  profile.addresses["person_r"] = 0x401000;
  profile.addresses["person_s"] = 0x402000;
  profile.addresses["person_face"] = 0x403000;
  profile.addresses["merit"] = 0x404000;
  profile.limits["person_count"] = 2;
  profile.limits["merit_count"] = 102;
  auto backend = fakeBackend(0x400000, 0x8000);
  putNumber(backend->regions[0x400000], 0, 4, 0x406000);
  backend->regions[0x406000] = Bytes(0x90);
  std::fill(backend->regions[0x406000].begin(),
            backend->regions[0x406000].end(), uint8_t(0xAA));
  auto session =
      std::make_shared<MemorySession>(backend, 0x400000, 0x8000);
  PersonService people(session, profile);
  auto row = people.read(0);
  row.hp = 123;
  row.strength = 456;
  row.baseStats = {1, 2, 3, 4, 5};
  row.merits = {10, 20, 30, 40, 50};
  people.write(0, row);
  const auto &personBytes = backend->regions[0x406000];
  CHECK(personBytes[0] == 0xAA && personBytes[5] == 0xAA);
  CHECK(number(personBytes, 0x1b, 4) == 123);
  CHECK(number(personBytes, 0x11, 2) == 456);
  CHECK(personBytes[0x21] == 1 && personBytes[0x25] == 5);
  CHECK(session->u(0x401000, 2, true) == 0);
  CHECK(session->u(0x404000, 2, true) == 10);

  EngineProfile battleProfile;
  battleProfile.version = EngineVersion::V64;
  battleProfile.addresses["battle_units"] = 0x400000;
  battleProfile.addresses["battle_sp"] = 0x405000;
  battleProfile.addresses["save_state"] = 0x407000;
  battleProfile.addresses["battle_state"] = 0x408000;
  battleProfile.limits["battle_ours"] = 1;
  battleProfile.limits["battle_friends"] = 0;
  battleProfile.limits["battle_enemies"] = 0;
  battleProfile.limits["battle_stride"] = 0x30;
  auto battleBackend = fakeBackend(0x400000, 0x9000);
  std::fill(battleBackend->regions[0x400000].begin(),
            battleBackend->regions[0x400000].begin() + 0x30, uint8_t(0xBB));
  battleBackend->regions[0x400000][6] = 1;
  battleBackend->regions[0x400000][7] = 0;
  battleBackend->regions[0x405000] = Bytes(0x100);
  battleBackend->regions[0x407000] = Bytes(0x20);
  battleBackend->regions[0x407000][6] = 1;
  battleBackend->regions[0x408000] = Bytes(0x20);
  auto battleSession =
      std::make_shared<MemorySession>(battleBackend, 0x400000, 0x9000);
  BattleService battle(battleSession, battleProfile);
  BattleRecord record{};
  record.dataId = 3;
  record.hp = 0x11223344;
  record.mp = 0x55667788;
  record.x = 5;
  record.y = 6;
  record.sp = 9;
  battle.write(0, record);
  const auto &battleBytes = battleBackend->regions[0x400000];
  CHECK(battleBytes[0x0b] == 0xBB);
  CHECK(battleBytes[0x1f] == 0xBB);
  CHECK(battleBytes[0x25] == 0xBB);
  CHECK(number(battleBytes, 0, 2) == 3);
  CHECK(number(battleBytes, 0x10, 4) == 0x11223344);
  CHECK(battleBackend->regions[0x405000][3] == 9);

  BattleState state{3, 7, 9, 4, 5};
  battle.writePage(0, record, state);
  CHECK(battleBackend->regions[0x408000][3] == 7);
  CHECK(battleBackend->regions[0x408000][2] == 2);
  CHECK(battleBackend->regions[0x408000][0x11] == 5);
  CHECK(battleBackend->regions[0x407000][6] == 17);

  auto warehouseBackend = fakeBackend(0x400000, 0x4000);
  EngineProfile warehouseProfile;
  warehouseProfile.addresses["warehouse"] = 0x401000;
  warehouseProfile.addresses["money"] = 0x403000;
  warehouseProfile.addresses["alignment"] = 0x403004;
  warehouseProfile.addresses["merit_pool"] = 0x403008;
  warehouseProfile.limits["warehouse_count"] = 2;
  auto warehouseSession =
      std::make_shared<MemorySession>(warehouseBackend, 0x400000, 0x4000);
  WarehouseService warehouse(warehouseSession, warehouseProfile);
  WarehousePageEdit edit;
  edit.index = 0;
  edit.record = {5, 6, 7};
  edit.writeRecord = true;
  edit.writeGlobals = true;
  edit.money = 0x11111111;
  edit.alignment = 3;
  edit.meritPool = 0x22222222;
  warehouse.writePage(edit);
  CHECK(warehouseBackend->regions[0x400000][0x1000] == 5);
  CHECK(number(warehouseBackend->regions[0x400000], 0x3000, 4) == 0x11111111);

  warehouseBackend->failWrite = 0x403000;
  WarehousePageEdit failing = edit;
  failing.record = {8, 9, 10};
  failing.money = 0x33333333;
  bool rejected = false;
  try {
    warehouse.writePage(failing);
  } catch (...) {
    rejected = true;
  }
  warehouseBackend->failWrite = 0;
  CHECK(rejected);
  CHECK(warehouseBackend->regions[0x400000][0x1000] == 5);
  CHECK(number(warehouseBackend->regions[0x400000], 0x3000, 4) == 0x11111111);

  EngineProfile effectProfile;
  effectProfile.version = EngineVersion::V64;
  effectProfile.addresses["effect_assign"] = 0x401000;
  effectProfile.limits["effect_count"] = 2;
  effectProfile.limits["effect_stride"] = 8;
  auto effectBackend = fakeBackend(0x400000, 0x4000);
  auto effectSession =
      std::make_shared<MemorySession>(effectBackend, 0x400000, 0x4000);
  EffectService effects(effectSession, effectProfile);
  EffectRecord effect{};
  effect.characters = {11, 22, 33, 0};
  effect.jobs[0] = 44;
  effect.jobValues[0] = 55;
  effects.write(0, effect);
  CHECK(number(effectBackend->regions[0x400000], 0x1000, 2) == 11);
  CHECK(number(effectBackend->regions[0x400000], 0x1004, 2) == 33);
  CHECK(effectBackend->regions[0x400000][0x1006] == 44);

  EngineProfile fatalProfile;
  fatalProfile.version = EngineVersion::V64;
  fatalProfile.addresses["fatal_table"] = 0x401000;
  fatalProfile.limits["fatal_count"] = 2;
  auto fatalBackend = fakeBackend(0x400000, 0x4000);
  auto fatalSession =
      std::make_shared<MemorySession>(fatalBackend, 0x400000, 0x4000);
  FatalService fatal(fatalSession, fatalProfile);
  FatalRecord fatalRow{};
  fatalRow.persons = {1, 2, 3, 4, 5};
  fatalRow.levels = {6, 7, 8, 9, 10};
  fatalRow.value = 99;
  fatal.write(0, fatalRow);
  CHECK(number(fatalBackend->regions[0x400000], 0x1000, 2) == 1);
  CHECK(fatalBackend->regions[0x400000][0x100f] == 99);
}

void testRemote() {
  const Bytes recalculate = {
      0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x08, 0xFF, 0x30, 0xFF,
      0x50, 0x04, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
  CHECK(remoteShell("recalculate", false) == recalculate);
  const Bytes revive = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x8B, 0x45, 0x08, 0x8B, 0x08,
      0x8B, 0x50, 0x04, 0x89, 0x4D, 0xF0, 0x8B, 0x48, 0x08, 0x89, 0x55,
      0xF8, 0x8B, 0x50, 0x0C, 0x8B, 0x40, 0x10, 0x89, 0x4D, 0xF4, 0x89,
      0x55, 0xFC, 0x89, 0x45, 0x08, 0xFF, 0x75, 0xF0, 0xFF, 0x75, 0xF4,
      0xFF, 0x75, 0xF8, 0xFF, 0x75, 0xFC, 0xFF, 0x55, 0x08, 0x33, 0xC0,
      0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
  CHECK(remoteShell("revive", false) == revive);
  const Bytes direction66 = {
      0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x6A, 0x01, 0x6A, 0x01, 0x6A,
      0x00, 0xFF, 0x30, 0x68, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x70, 0x04,
      0xFF, 0x50, 0x08, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
  CHECK(remoteShell("changeDirection", true) == direction66);
  const Bytes directionLegacy = {
      0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x6A, 0x00, 0xFF, 0x30, 0x68,
      0xFF, 0xFF, 0x00, 0x00, 0xFF, 0x70, 0x04, 0x8B, 0x48, 0x08, 0xFF,
      0x50, 0x0C, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
  CHECK(remoteShell("changeDirection", false) == directionLegacy);
  const Bytes position = {
      0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xFF, 0x30, 0xFF, 0x70, 0x04,
      0xFF, 0x70, 0x08, 0xFF, 0x70, 0x0C, 0x8B, 0x48, 0x10, 0xFF, 0x50,
      0x14, 0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
  CHECK(remoteShell("changePosition", false) == position);
  const Bytes weather = {0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0xFF, 0x30,
                         0xB9, 0,    0,    0,    0,    0xFF, 0x50, 0x04,
                         0x33, 0xC0, 0x8B, 0xE5, 0x5D, 0xC2, 0x04, 0x00};
  CHECK(remoteShell("changeWeather", false) == weather);
  for (const auto *key : {"recalculate", "revive", "changePosition",
                          "changeWeather"}) {
    const auto shell = remoteShell(key, false);
    CHECK(shell.size() >= 8 && shell[shell.size() - 8] == 0x33 &&
          shell[shell.size() - 7] == 0xC0);
  }
  // 6.6 的坐标刷新调用约定未经验证，服务层必须在触碰内存之前就拒绝。
  {
    auto backend = std::make_shared<FakeMemoryBackend>();
    auto session =
        std::make_shared<MemorySession>(backend, 0x400000, 0x200000);
    EngineProfile profile66;
    profile66.version = EngineVersion::V66;
    bool rejected = false;
    try {
      RemoteServices services(session, profile66);
      services.changePosition(0, 1, 2, 3);
    } catch (const std::exception &) {
      rejected = true;
    }
    CHECK(rejected);
    CHECK(backend->writes.empty());
  }
}

void testEvidence() {
  Json value;
  value["beta"] = Json::array({true, nullptr, "x"});
  value["alpha"] = 1;
  const auto canonical = evidenceCanonicalJson(value);
  CHECK(canonical == "{\"alpha\":1,\"beta\":[true,null,\"x\"]}");
  CHECK(evidenceDigest(value) ==
        sha256(Bytes(canonical.begin(), canonical.end())));
  Json recipe;
  recipe["kind"] = "test";
  auto recipeId = evidenceRecipeId("feature", recipe);
  CHECK(recipeId.rfind("ccz66.feature.", 0) == 0);
  CHECK(recipeId.size() == std::string("ccz66.feature.").size() + 20);
  CHECK(evidenceRequiredRuntimeChecks("control_auto_return").size() == 12);
  CHECK(evidenceRequiredRuntimeChecks("unknown").size() == 2);

  Json bundle;
  bundle["schemaVersion"] = "1.0";
  bundle["featureKey"] = "feature";
  bundle["target"] = {{"sha256", std::string(64, 'A')}, {"fileSize", 1}};
  bundle["recipeId"] = recipeId;
  bundle["recipe"] = recipe;
  bundle["phase"] = "static";
  bundle["staticEvidence"] = Json::object();
  bundle["runtimeSamples"] = Json::array();
  bundle["verification"] = {{"collector", kEvidenceCollectorId},
                            {"status", "captured"},
                            {"automated", true},
                            {"checks", Json::object()}};
  auto finalized = evidenceFinalizeBundle(bundle);
  evidenceValidateBundle(finalized, false);
  auto tampered = finalized;
  tampered["featureKey"] = "other";
  bool rejected = false;
  try {
    evidenceValidateBundle(tampered, false);
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);

  auto sample65 = envPath(L"BANSHOU_SAMPLE_65");
  if (!sample65.empty() && std::filesystem::exists(std::filesystem::path(sample65))) {
    auto output = tempDirectory(L"banshou_parity_evidence");
    std::vector<std::wstring> arguments = {L"tool", L"--evidence-static",
                                          L"--exe", sample65, L"--output",
                                          output};
    std::vector<wchar_t *> argv;
    for (auto &argument : arguments)
      argv.push_back(argument.data());
    CHECK(evidenceCli(int(argv.size()), argv.data()) == 0);
    size_t bundles = 0;
    for (const auto &entry :
         std::filesystem::directory_iterator(std::filesystem::path(output))) {
      if (entry.path().extension() != L".json" ||
          entry.path().filename() == kEvidenceManifestName)
        continue;
      auto bundleJson = readJsonFile(entry.path().wstring());
      evidenceValidateBundle(bundleJson, false);
      ++bundles;
    }
    CHECK(bundles >= 20);

    std::vector<std::wstring> oracleArguments = {
        L"tool", L"--evidence-oracle", L"--exe", sample65, L"--output", output,
        L"--legacy-index", L"1"};
    argv.clear();
    for (auto &argument : oracleArguments)
      argv.push_back(argument.data());
    CHECK(evidenceCli(int(argv.size()), argv.data()) == 0);
    bool sawOracle = false;
    for (const auto &entry :
         std::filesystem::directory_iterator(std::filesystem::path(output))) {
      if (entry.path().filename().string().rfind("legacy_oracle-", 0) != 0)
        continue;
      auto bundleJson = readJsonFile(entry.path().wstring());
      const auto hexText =
          bundleJson["staticEvidence"]["autoReturnStub"]
              .value("reconstructedHex", std::string());
      CHECK(hexText.size() == 0x49 * 2);
      sawOracle = true;
    }
    CHECK(sawOracle);
    std::filesystem::remove_all(output);
  }

  auto mock = parentPath(exePath()) + L"\\mock_target.exe";
  if (std::filesystem::exists(std::filesystem::path(mock))) {
    auto output = tempDirectory(L"banshou_parity_runtime");
    auto addressFile = output + L"\\spin.addr";
    auto recipeFile = output + L"\\recipe.json";
    writeText(recipeFile,
              "{\"kind\":\"runtime-test\",\"verificationContract\":{\"checks\":"
              "{\"runtime_probe\":{\"type\":\"min-samples\",\"minimum\":1},"
              "\"behavior_verified\":{\"type\":\"field-present\","
              "\"path\":\"registers.Eax\"}}}}");
    std::wstring command = L"\"" + mock +
                           L"\" --breakpoint-spin --address-file \"" +
                           addressFile + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &startup, &process)) {
      std::wstring reference;
      for (int attempt = 0; attempt < 100 && reference.empty(); ++attempt) {
        if (std::filesystem::exists(std::filesystem::path(addressFile))) {
          auto bytes = readFile(addressFile);
          if (bytes.size() >= 2) {
            std::wstring text(bytes.size() / 2, L'\0');
            std::memcpy(text.data(), bytes.data(), bytes.size());
            if (!text.empty() && text[0] == 0xfeff)
              text.erase(text.begin());
            reference = text;
          }
        }
        if (reference.empty())
          Sleep(50);
      }
      if (!reference.empty()) {
        while (!reference.empty() &&
               (reference.back() == L'\r' || reference.back() == L'\n' ||
                reference.back() == L' '))
          reference.pop_back();
        std::vector<std::wstring> arguments = {
            L"tool",
            L"--evidence-runtime",
            L"--pid",
            std::to_wstring(process.dwProcessId),
            L"--feature",
            L"runtime_probe",
            L"--output",
            output,
            L"--breakpoint",
            reference,
            L"--samples",
            L"1",
            L"--timeout",
            L"10",
            L"--recipe",
            recipeFile,
            L"--tag",
            L"phase=test"};
        std::vector<wchar_t *> argv;
        for (auto &argument : arguments)
          argv.push_back(argument.data());
        CHECK(evidenceCli(int(argv.size()), argv.data()) == 0);
        std::wstring captured;
        for (const auto &entry :
             std::filesystem::directory_iterator(std::filesystem::path(output))) {
          if (entry.path().filename().string().find("-runtime.json") !=
              std::string::npos)
            captured = entry.path().wstring();
        }
        CHECK(!captured.empty());
        if (!captured.empty()) {
          std::vector<std::wstring> verifyArguments = {
              L"tool", L"--evidence-verify", captured, L"--output", output};
          argv.clear();
          for (auto &argument : verifyArguments)
            argv.push_back(argument.data());
          CHECK(evidenceCli(int(argv.size()), argv.data()) == 0);
          std::vector<std::wstring> inspectArguments = {L"tool",
                                                        L"--evidence-inspect",
                                                        captured};
          argv.clear();
          for (auto &argument : inspectArguments)
            argv.push_back(argument.data());
          CHECK(evidenceCli(int(argv.size()), argv.data()) == 0);
        }
      }
      TerminateProcess(process.hProcess, 0);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
    }
    std::filesystem::remove_all(output);
  }
}

void testCli() {
  std::vector<std::wstring> noCommand = {L"tool"};
  std::vector<wchar_t *> argv;
  for (auto &argument : noCommand)
    argv.push_back(argument.data());
  CHECK(evidenceCli(int(argv.size()), argv.data()) == 2);
  std::vector<std::wstring> unknown = {L"tool", L"--not-a-command"};
  argv.clear();
  for (auto &argument : unknown)
    argv.push_back(argument.data());
  CHECK(evidenceCli(int(argv.size()), argv.data()) == 2);
  std::vector<std::wstring> missing = {L"tool", L"--evidence-static"};
  argv.clear();
  for (auto &argument : missing)
    argv.push_back(argument.data());
  CHECK(evidenceCli(int(argv.size()), argv.data()) == 1);
  std::vector<std::wstring> inspectMissing = {L"tool", L"--evidence-inspect"};
  argv.clear();
  for (auto &argument : inspectMissing)
    argv.push_back(argument.data());
  CHECK(evidenceCli(int(argv.size()), argv.data()) == 1);
}

void testStartup() {
  auto exe = sourceRoot() + L"\\dist\\扳手.exe";
  if (!std::filesystem::exists(std::filesystem::path(exe))) {
    std::cout << "startup smoke skipped: dist executable missing\n";
    return;
  }
  auto output = tempDirectory(L"banshou_parity_startup") + L"\\smoke.json";
  std::wstring command = L"\"" + exe +
                         L"\" --startup-smoke-output \"" + output +
                         L"\" --startup-smoke-exit";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0,
                      nullptr, nullptr, &startup, &process)) {
    std::cerr << "startup smoke launch failed\n";
    ++failures;
    return;
  }
  const auto wait = WaitForSingleObject(process.hProcess, 20000);
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CHECK(wait == WAIT_OBJECT_0);
  CHECK(code == 0);
  if (std::filesystem::exists(std::filesystem::path(output))) {
    auto report = readJsonFile(output);
    CHECK(report.value("schema", std::string()) ==
          "banshou-startup-smoke-v1");
    CHECK(report.value("success", false));
    CHECK(report.value("gameMemoryWrites", true) == false);
    CHECK(report.value("remoteCalls", true) == false);
    CHECK(report["pages"].is_array() && report["pages"].size() == 8);
  } else {
    CHECK(false);
  }
  std::filesystem::remove_all(std::filesystem::path(output).parent_path());
}

void testCoverage() {
  const auto baselineBytes =
      readFile(sourceRoot() + L"\\resources\\baseline.json");
  auto baseline = Json::parse(
      std::string(baselineBytes.begin(), baselineBytes.end()));
  CHECK(baseline.value("schema", 0) == 2);
  const auto &tests = baseline["tests"];
  CHECK(tests.is_array() && tests.size() == 399);
  const std::set<std::string> suites = {
      "native_contracts",          "lifecycle",
      "qt_ui_contracts",           "memory_contracts",
      "profile_contracts",         "item_contracts",
      "variable_contracts",        "transaction_contracts",
      "evidence_contracts",        "cli_contracts",
      "startup_contracts",         "coverage_contracts",
      "verify_release_contracts",  "platform_contracts"};
  const std::set<std::string> reasons = {"python-build-script",
                                         "python-internal-abstraction"};
  size_t covered = 0, notApplicable = 0;
  for (const auto &test : tests) {
    const auto status = test.value("nativeStatus", std::string());
    CHECK(status != "pending");
    if (status.rfind("covered:", 0) == 0)
      CHECK(suites.count(status.substr(8)) == 1);
    else if (status.rfind("not-applicable:", 0) == 0)
      CHECK(reasons.count(status.substr(15)) == 1);
    else
      CHECK(false);
    if (status.rfind("covered:", 0) == 0)
      ++covered;
    else
      ++notApplicable;
  }
  CHECK(covered + notApplicable == 399);

  std::ifstream map(std::filesystem::path(sourceRoot()) / L"docs" /
                    L"native_contract_map.csv");
  CHECK(map.good());
  std::set<std::string> mapped;
  std::string line;
  while (std::getline(map, line)) {
    auto first = line.find(',');
    if (first == std::string::npos || line.rfind("source,", 0) == 0)
      continue;
    mapped.insert(line.substr(0, first));
  }
  for (const auto &test : tests)
    CHECK(mapped.count(test.value("source", std::string())) == 1);
}

void testVerifyRelease() {
  auto exe = sourceRoot() + L"\\dist\\扳手.exe";
  CHECK(std::filesystem::exists(std::filesystem::path(exe)));
  auto shaFile = exe + L".sha256";
  if (std::filesystem::exists(std::filesystem::path(shaFile))) {
    const auto shaBytes = readFile(shaFile);
    auto text = std::string(shaBytes.begin(), shaBytes.end());
    auto expected = text.substr(0, text.find_first_of(" \r\n\t"));
    CHECK(evidenceCanonicalJson(Json::object()) == "{}");
    CHECK(expected.size() == 64);
    auto actual = sha256(readFile(exe));
    CHECK(expected == actual);
  } else {
    std::cout << "release sha file not present yet\n";
  }
  CHECK(std::filesystem::exists(
      std::filesystem::path(sourceRoot() + L"\\tools\\verify_release.ps1")));
}

void testPlatform() {
  const std::wstring text = L"扳手_中文_路径";
  CHECK(wide(utf8(text)) == text);
  auto absolute = fullPath(L"relative.txt");
  CHECK(absolute.rfind(L"\\\\?\\", 0) == 0 && absolute.size() > 10);
  CHECK(sha256(Bytes{'a', 'b', 'c'}) ==
        "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
  CHECK(hex(unhex("00 01 FF")) == "0001FF");
  auto catalog = resourceJson(103);
  CHECK(catalog.value("schema", 0) == 2);
  CHECK(catalog["patterns"].is_object() && !catalog["patterns"].empty());
  CHECK(catalog["known66"].is_object());
  auto json = Json::parse(
      std::string("{\"name\":\"") + utf8(text) + "\"}");
  CHECK(json["name"].get<std::string>() == utf8(text));
  bool rejected = false;
  try {
    checkedAddress(0xFFFFFFFF, 8, 1);
  } catch (...) {
    rejected = true;
  }
  CHECK(rejected);
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: parity_tests <mode>\n";
    return 2;
  }
  const std::string mode = argv[1];
  try {
    if (mode == "memory")
      testMemory();
    else if (mode == "profile")
      testProfile();
    else if (mode == "item")
      testItem();
    else if (mode == "variable")
      testVariable();
    else if (mode == "transaction")
      testTransaction();
    else if (mode == "remote")
      testRemote();
    else if (mode == "evidence")
      testEvidence();
    else if (mode == "cli")
      testCli();
    else if (mode == "startup")
      testStartup();
    else if (mode == "coverage")
      testCoverage();
    else if (mode == "verify_release")
      testVerifyRelease();
    else if (mode == "platform")
      testPlatform();
    else {
      std::cerr << "unknown mode: " << mode << "\n";
      return 2;
    }
  } catch (const std::exception &error) {
    std::cerr << "fatal: " << error.what() << "\n";
    return 1;
  }
  if (failures) {
    std::cerr << failures << " checks failed\n";
    return 1;
  }
  std::cout << mode << " contracts passed\n";
  return 0;
}
