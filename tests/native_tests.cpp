#include "diy.hpp"
#include "game.hpp"
#include "variables.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
using namespace banshou;
int main() {
  assert(hex(unhex("00 01 ff")) == "0001FF");
  auto instructions = disassembleX86(Bytes{0x55, 0x8b, 0xec, 0xc3});
  assert(instructions.size() == 3);
  auto unicodePath =
      std::filesystem::temp_directory_path() / L"扳手_中文_路径.dat";
  Bytes fileData{1, 2, 3, 4};
  writeFile(unicodePath.wstring(), fileData);
  assert(readFile(unicodePath.wstring()) == fileData);
  DeleteFileW(unicodePath.wstring().c_str());
  Bytes b(8);
  putNumber(b, 0, 4, 0x12345678);
  assert(number(b, 0, 4) == 0x12345678);
  auto fake = std::make_shared<FakeMemoryBackend>();
  fake->regions[0x400000] = Bytes(0x2000);
  MemorySession s(fake, 0x400000, 0x2000);
  s.writeU(0x400010, 0x1234, 2);
  assert(s.u(0x400010, 2) == 0x1234);
  auto mem = std::make_shared<FakeMemoryBackend>();
  mem->regions[0x400000] = Bytes(0x1000);
  mem->regions[0x500000] = Bytes(0x48);
  mem->regions[0x50f000] = Bytes(0x2000);
  mem->regions[0x501000] = Bytes(0x2000);
  mem->regions[0x50f800] = Bytes(0x2000);
  mem->regions[0x508000] = Bytes(0x2000);
  putNumber(mem->regions[0x400000], 0, 4, 0x500000);
  EngineProfile p;
  p.version = EngineVersion::V66;
  p.limits["person_count"] = 1;
  p.addresses["person_pointer"] = 0x400000;
  MemorySession ps(mem, 0x400000, 0x200000);
  PersonService people(std::make_shared<MemorySession>(ps), p);
  auto row = people.read(0);
  assert(row.id == 0);
  row.hp = 123;
  row.strength = 456;
  row.defense = 457;
  row.intelligence = 458;
  row.baseStats = {91, 92, 93, 94, 95};
  row.appearance = 3;
  row.retreat = 4;
  people.write(0, row);
  assert(number(mem->regions[0x500000], 0x1b, 4) == 123);
  assert(number(mem->regions[0x500000], 0x11, 2) == 456);
  assert(number(mem->regions[0x500000], 0x13, 2) == 457);
  assert(number(mem->regions[0x500000], 0x15, 2) == 458);
  assert(mem->regions[0x500000][0x21] == 91);
  assert(mem->regions[0x500000][0x25] == 95);
  assert(mem->regions[0x500000][0x27] == 3);
  assert(mem->regions[0x500000][0x29] == 4);
  ProfileRegistry profiles;
  assert(profiles.legacy(0).version == EngineVersion::V64);
  assert(profiles.legacy(1).version == EngineVersion::V63MP);
  assert(profiles.legacy(2).version == EngineVersion::V63);
  assert(profiles.legacy(3).version == EngineVersion::V62);
  assert(profiles.legacy(4).version == EngineVersion::V61);
  assert(profiles.legacy(5).version == EngineVersion::V65);
  assert(profiles.legacy(6).version == EngineVersion::V66);
  assert(profiles.legacy(0).remoteAddress("revive") == 0x004092C7);
  assert(profiles.legacy(4).remoteAddress("revive") == 0x0040930F);
  assert(profiles.legacy(1).remoteAddress("revive") == 0x004092E0);
  assert(profiles.legacy(6).remoteAddress("revive") == 0x004092C0);
  assert(profiles.legacy(6).consumableItemIds.size() == 352);
  assert(profiles.legacy(6).consumableItemIds.front() == 160);
  assert(profiles.legacy(6).consumableItemIds.back() == 511);
  assert(profiles.legacy(6).limit("variable_ptr_count") == 481);
  assert(profiles.legacy(6).boolVariables.storage == VariableStorage::Bitset);
  assert(profiles.legacy(6).boolVariables.browseCount == 2048);
  assert(profiles.legacy(0).limit("variable_ptr_count") == 481);
  assert(profiles.legacy(0).limit("item_name_size") == 17);
  assert(profiles.legacy(0).boolVariables.storage == VariableStorage::Array);
  assert(profiles.legacy(0).boolVariables.stride == 4);
  auto patchMem = std::make_shared<FakeMemoryBackend>();
  patchMem->regions[0x400000] = Bytes(0x90000);
  patchMem->regions[0x400000][0x4E570] = 0xE8;
  auto patchSession =
      std::make_shared<MemorySession>(patchMem, 0x400000, 0x90000);
  auto patchProfile = profiles.legacy(0);
  bool legacyAutoRejected = false;
  try {
    LegacyAutoReturnPatch::set(patchSession, patchProfile, true);
  } catch (const Error &) {
    legacyAutoRejected = true;
  }
  assert(legacyAutoRejected);
  auto stub = LegacyAutoReturnPatch::buildStub(
      patchSession->read(0x00485E00, 0x49), patchProfile);
  assert(number(stub, 0, 4) == 0x83EC8B55);
  assert(number(stub, 0x19, 1) == (patchProfile.limit("battle_stride") & 0xff));
  assert(number(stub, 0x1c, 3) == patchProfile.address("battle_units"));
  PatchManager patchManager(patchSession);
  bool stalePatchRejected = false;
  try {
    patchManager.apply(0x00485E00, Bytes{0xAA}, Bytes{0x90});
  } catch (const Error &) {
    stalePatchRejected = true;
  }
  assert(stalePatchRejected);
  auto patchOriginal = patchSession->readActual(0x00485E00, 1);
  patchManager.apply(0x00485E00, patchOriginal, Bytes{0x90});
  assert(patchSession->readActual(0x00485E00, 1) == Bytes({0x90}));
  patchManager.restore(0x00485E00, Bytes{0x90}, patchOriginal);
  assert(patchSession->readActual(0x00485E00, 1) == patchOriginal);
  auto consumableMem = std::make_shared<FakeMemoryBackend>();
  consumableMem->regions[0x400000] = Bytes(0x140000);
  auto consumableSession =
      std::make_shared<MemorySession>(consumableMem, 0x400000, 0x140000);
  EngineProfile profile66 = profiles.legacy(6);
  profile66.consumables.key = "standard-66-352-v1";
  profile66.consumables.base = 0x00493AF0;
  profile66.consumables.idMin = 160;
  profile66.consumables.idMax = 511;
  profile66.consumables.storageCount = 352;
  profile66.consumables.equipmentCount = 160;
  for (uint32_t id = 160; id <= 511; ++id)
    profile66.consumables.itemIds.push_back(uint16_t(id));
  profile66.addresses["consumable_counts"] = 0x00493AF0;
  profile66.limits["consumable_storage_count"] = 352;
  WarehouseService warehouse66(consumableSession, profile66);
  assert(warehouse66.consumableCount() == 352);
  assert(warehouse66.consumableItemId(0) == 160);
  assert(warehouse66.consumableItemId(351) == 511);
  warehouse66.setAllConsumables(0xff);
  assert(consumableSession->u(0x00493AF0, 1) == 0xff);
  assert(consumableSession->u(0x00493C4F, 1) == 0xff);
  WarehouseService warehouse61(consumableSession, profiles.legacy(4));
  assert(warehouse61.consumableCount() == 43);
  warehouse61.writeConsumable(16, 7);
  warehouse61.writeConsumable(17, 9);
  assert(consumableSession->u(0x004B09EB, 1) == 7);
  assert(consumableSession->u(0x00510C80, 1) == 9);
  auto stateMem = std::make_shared<FakeMemoryBackend>();
  stateMem->regions[0x400000] = Bytes(0x5000);
  EngineProfile stateProfile;
  stateProfile.addresses["battle_state"] = 0x403000;
  stateProfile.addresses["save_state"] = 0x402000;
  MemorySession stateSession(stateMem, 0x400000, 0x5000);
  stateMem->regions[0x400000][0x3000 + 4] = 0x16;
  stateMem->regions[0x400000][0x3000 + 0x11] = 0;
  assert(BattleStateService(std::make_shared<MemorySession>(stateSession),
                            stateProfile)
             .read()
             .weather == 4);
  BattleState next{7, 3, 9, 4, 5};
  BattleStateService state(std::make_shared<MemorySession>(stateSession),
                           stateProfile);
  state.write(next);
  assert(stateMem->regions[0x400000][0x3000 + 4] == 0x16);
  assert(stateMem->regions[0x400000][0x3000 + 0x11] == 5);
  for (uint8_t season = 0; season < 5; ++season) {
    stateMem->regions[0x400000][0x3000 + 0x11] = season;
    for (uint8_t raw = 0; raw < 6; ++raw) {
      stateMem->regions[0x400000][0x3000 + 4] = raw;
      auto weather = BattleStateService(
                         std::make_shared<MemorySession>(stateSession),
                         stateProfile)
                         .read()
                         .weather;
      assert(weather <= 4);
    }
  }
  auto battleMem = std::make_shared<FakeMemoryBackend>();
  battleMem->regions[0x400000] = Bytes(0x30000);
  EngineProfile battleProfile;
  battleProfile.addresses["battle_units"] = 0x401000;
  battleProfile.addresses["battle_sp"] = 0x420000;
  battleProfile.addresses["save_state"] = 0x402000;
  battleProfile.limits["battle_ours"] = 1;
  battleProfile.limits["battle_friends"] = 0;
  battleProfile.limits["battle_enemies"] = 0;
  battleProfile.limits["battle_stride"] = 0x30;
  battleMem->regions[0x400000][0x1000] = 0xff;
  battleMem->regions[0x400000][0x1001] = 0xff;
  BattleService battle(
      std::make_shared<MemorySession>(battleMem, 0x400000, 0x30000),
      battleProfile);
  auto empty = battle.read(0);
  assert(empty.dataId == 0xffff && empty.sp == 0);
  bool threw = false;
  try {
    battle.read(1);
  } catch (const Error &) {
    threw = true;
  }
  assert(threw);
  battleMem->regions[0x400000][0x2000 + 6] = 1;
  BattleRecord strategyRow{};
  strategyRow.dataId = 1;
  strategyRow.ai = 4;
  strategyRow.targetSlot = 0xff;
  strategyRow.strategyX = 21;
  strategyRow.strategyY = 22;
  strategyRow.targetX = 31;
  strategyRow.targetY = 32;
  battle.write(0, strategyRow);
  auto strategyRoundTrip = battle.read(0);
  assert(strategyRoundTrip.ai == 4 && strategyRoundTrip.targetSlot == 0xff);
  assert(strategyRoundTrip.strategyX == 21 &&
         strategyRoundTrip.strategyY == 22);
  assert(strategyRoundTrip.targetX == 31 && strategyRoundTrip.targetY == 32);
  battleMem->regions[0x400000][0x2000 + 6] = 0;
  battleMem->regions[0x400000][0x1010] = 0xAA;
  battleMem->regions[0x400000][0x1014] = 0xBB;
  BattleRecord hpOnly{};
  hpOnly.dataId = 0x1234;
  hpOnly.hp = 0x11223344;
  hpOnly.mp = 0x55667788;
  battle.write(0, hpOnly);
  assert(number(battleMem->regions[0x400000], 0x1010, 4) == 0x11223344);
  assert(number(battleMem->regions[0x400000], 0x1014, 4) == 0x55667788);
  assert(number(battleMem->regions[0x400000], 0x1000, 2) == 1);
  auto variableMem = std::make_shared<FakeMemoryBackend>();
  variableMem->regions[0x400000] = Bytes(0x30000);
  EngineProfile variableProfile;
  variableProfile.addresses["bool_vars"] = 0x401000;
  variableProfile.addresses["int_vars"] = 0x402000;
  variableProfile.addresses["ptr_vars"] = 0x403000;
  variableProfile.boolVariables.base = 0x401000;
  variableProfile.boolVariables.storage = VariableStorage::Bitset;
  variableProfile.boolVariables.stride = 1;
  variableProfile.boolVariables.width = 1;
  variableProfile.boolVariables.recordCount = 1536;
  variableProfile.boolVariables.writableCount = 1536;
  variableProfile.boolVariables.browseCount = 2048;
  variableProfile.intVariables.base = 0x402000;
  variableProfile.intVariables.recordCount = 4096;
  variableProfile.intVariables.writableCount = 4096;
  variableProfile.intVariables.browseCount = 4096;
  variableProfile.ptrVariables.base = 0x403000;
  variableProfile.ptrVariables.recordCount = 481;
  variableProfile.ptrVariables.writableCount = 481;
  variableProfile.ptrVariables.browseCount = 481;
  auto variableSession =
      std::make_shared<MemorySession>(variableMem, 0x400000, 0x30000);
  VariableService variables(variableSession, variableProfile);
  variables.writeBool(1, true);
  variables.writeBool(1535, true);
  variables.writeInt(2, -7);
  variables.writePointer(480, 0x12345678);
  assert(variables.readBool(1) && variables.readBool(1535) &&
         variables.readInt(2) == -7 &&
         variables.readPointer(480) == 0x12345678);
  bool readonlyRejected = false;
  try {
    variables.writeBool(1536, true);
  } catch (const Error &) {
    readonlyRejected = true;
  }
  assert(readonlyRejected);
  bool pointerRejected = false;
  try {
    variables.readPointer(481);
  } catch (const Error &) {
    pointerRejected = true;
  }
  assert(pointerRejected);
  assert(variables.readPage(VariableKind::Pointer, 480, 1)[0].address ==
         0x403780);
  std::vector<bool> flags(16, false);
  flags[0] = true;
  flags[9] = true;
  auto booleanArchive = encodeBooleanArchive(flags, flags.size());
  assert(booleanArchive.size() == 16 + 2);
  auto decodedFlags = decodeBooleanArchive(booleanArchive, flags.size());
  assert(decodedFlags == flags);
  auto corruptArchive = booleanArchive;
  corruptArchive[0] = 'X';
  bool archiveRejected = false;
  try {
    decodeBooleanArchive(corruptArchive, flags.size());
  } catch (const Error &) {
    archiveRejected = true;
  }
  assert(archiveRejected);
  auto layoutDir = std::filesystem::temp_directory_path() / L"扳手_item_layout";
  std::filesystem::create_directories(layoutDir);
  writeFile((layoutDir / L"Data.e5").wstring(), Bytes(35764, 0));
  writeFile((layoutDir / L"Star.e5").wstring(), Bytes(3800, 0));
  auto layoutProfile = profiles.legacy(6);
  applyItemFileLayout(layoutProfile, (layoutDir / L"game.exe").wstring());
  assert(layoutProfile.limit("item_count") == 255);
  assert(layoutProfile.limit("item_record_count") == 256);
  writeFile((layoutDir / L"Star.e5").wstring(), Bytes(0x27D8, 0));
  applyItemFileLayout(layoutProfile, (layoutDir / L"game.exe").wstring());
  assert(layoutProfile.limit("item_count") == 512);
  assert(layoutProfile.limit("item_record_count") == 512);
  std::filesystem::remove_all(layoutDir);
  auto csvPath = std::filesystem::temp_directory_path() / L"扳手_DIY_中文.csv";
  std::string csv = "\xEF\xBB\xBF章节一,,,,,\r\n名称,战场编号,00400000,1,3,"
                    "说明\r\n跨行,宝物,00400010,2,1,\"含逗号,"
                    "和换行\n说明\"\r\n章节二,,,,,\r\n值,,00400020,4,1,尾\r\n";
  writeText(csvPath.wstring(), csv);
  DiyCsv diy;
  diy.load(csvPath.wstring());
  assert(diy.sectionCount() == 2);
  auto expanded = diy.expand(0, 0, 10);
  assert(expanded.size() == 4);
  assert(expanded[3].explanation.find("换行") != std::string::npos);
  assert(expanded[0].address == 0x400000 && expanded[3].width == 2);
  auto csvOut = csvPath.wstring() + L".out";
  diy.save(csvOut);
  auto saved = readFile(csvOut);
  assert(saved.size() >= 3 && saved[0] == 0xEF && saved[1] == 0xBB &&
         saved[2] == 0xBF);
  DeleteFileW(csvPath.wstring().c_str());
  DeleteFileW(csvOut.c_str());
  auto extraColumnsPath =
      std::filesystem::temp_directory_path() / L"扳手_DIY_extra_columns.csv";
  writeText(extraColumnsPath.wstring(),
            "章节,,,,,\r\n"
            "值,,00400030,1,1,说明,用户列\r\n"
            "溢出,,FFFFFFFE,1,4,边界,\r\n");
  DiyCsv extraColumns;
  extraColumns.load(extraColumnsPath.wstring());
  assert(extraColumns.rows().size() == 3);
  assert(extraColumns.rows()[0].trailingColumns.empty());
  assert(extraColumns.rows()[1].trailingColumns.size() == 1);
  assert(extraColumns.rows()[2].trailingColumns.size() == 1);
  auto extraColumnsOut = extraColumnsPath.wstring() + L".out";
  extraColumns.save(extraColumnsOut);
  auto extraSaved = readFile(extraColumnsOut);
  std::string extraText(extraSaved.begin() + 3, extraSaved.end());
  assert(extraText.find(",用户列\r\n") != std::string::npos);
  assert(extraColumns.page(SIZE_MAX, 4).empty());
  auto overflowExpanded = extraColumns.expand(0, 0, 10);
  assert(overflowExpanded.size() == 3 &&
         overflowExpanded[0].address == 0x400030 &&
         overflowExpanded[1].address == 0xFFFFFFFE &&
         overflowExpanded[2].address == 0xFFFFFFFF);
  DeleteFileW(extraColumnsPath.wstring().c_str());
  DeleteFileW(extraColumnsOut.c_str());
  auto noBomPath = std::filesystem::temp_directory_path() / L"扳手_DIY_no_bom.csv";
  writeText(noBomPath.wstring(), "章节,,,,,\r\n名称,宝物,00400000,1,1,\"a,b\"\r\n");
  DiyCsv noBom;
  noBom.load(noBomPath.wstring());
  assert(noBom.sectionCount() == 1);
  auto noBomOut = noBomPath.wstring() + L".out";
  noBom.save(noBomOut);
  auto noBomBytes = readFile(noBomOut);
  assert(noBomBytes.size() >= 3 && noBomBytes[0] == 0xEF &&
         noBomBytes[1] == 0xBB && noBomBytes[2] == 0xBF);
  DeleteFileW(noBomPath.wstring().c_str());
  DeleteFileW(noBomOut.c_str());
  return 0;
}
