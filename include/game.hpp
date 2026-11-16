#pragma once
#include "profile.hpp"
#include <array>
namespace banshou {
struct PersonRecord {
  uint16_t id = 0, r = 0, s = 0, face = 0, strength = 0, intelligence = 0,
           agility = 0, luck = 0, defense = 0;
  uint32_t hp = 0;
  uint16_t mp = 0;
  uint8_t ours = 0, level = 0, exp = 0, job = 0, weapon = 0, weaponLevel = 0,
          weaponExp = 0, armor = 0, armorLevel = 0, armorExp = 0, accessory = 0,
          field28 = 0;
  uint16_t field06 = 0, field44 = 0, field46 = 0, kills = 0;
  std::array<uint8_t, 5> baseStats{};
  uint8_t appearance = 0, retreat = 0;
  std::array<uint16_t, 5> merits{};
};
struct BattleRecord {
  uint16_t dataId = 0;
  uint8_t displaySlot = 0, faction = 0, x = 0, y = 0, targetSlot = 0,
          strategyX = 0, strategyY = 0, action = 0, ai = 0, direction = 0,
          visibility = 0, targetX = 0, targetY = 0, field22 = 0, field23 = 0;
  uint32_t hp = 0, mp = 0;
  uint8_t sp = 0, health = 0;
  std::array<uint8_t, 6> buffs{};
  std::array<uint8_t, 6> expanded{};
};
class PersonService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;

public:
  PersonService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  PersonRecord read(uint16_t id) const;
  void write(uint16_t id, const PersonRecord &value);
  std::vector<PersonRecord> readRange(size_t start, size_t count) const;
};
struct BattleState {
  uint8_t currentTurn = 0, turnLimit = 0, nextScenario = 0, weather = 0,
          season = 0;
};
class BattleService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;

public:
  BattleService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  BattleRecord read(size_t slot) const;
  void write(size_t slot, const BattleRecord &value);
  // 多槽位一次性提交：任一槽失败则整批回滚（区域灭敌等批量操作使用）。
  void writeRows(const std::vector<std::pair<size_t, BattleRecord>> &rows);
  void writePage(size_t slot, const BattleRecord &value,
                 const BattleState &state);
};
class BattleStateService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;

public:
  BattleStateService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  BattleState read() const;
  void write(const BattleState &value);
  void applyGroupBuff(size_t first, size_t last, size_t buff, int value);
};
struct WarehouseRecord {
  uint8_t item = 255, level = 0, experience = 0;
};
struct WarehousePageEdit {
  size_t index = 0;
  WarehouseRecord record;
  bool writeRecord = false;
  bool writeConsumable = false;
  size_t consumableIndex = 0;
  uint8_t consumableValue = 0;
  bool writeGlobals = false;
  uint32_t money = 0;
  uint8_t alignment = 0;
  uint32_t meritPool = 0;
};
class WarehouseService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;
  uint32_t consumableAddress(size_t index) const;

public:
  WarehouseService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  uint8_t read(size_t index) const;
  WarehouseRecord readRecord(size_t index) const;
  // 一次读取全部仓库记录（200×3 字节），失败时回退到逐条读取。
  std::vector<WarehouseRecord> readRecords(size_t count = 200) const;
  void write(size_t index, uint8_t value);
  void writeRecord(size_t index, const WarehouseRecord &value);
  void writePage(const WarehousePageEdit &edit);
  void clearWarehouse();
  size_t consumableCount() const;
  uint16_t consumableItemId(size_t index) const;
  uint8_t readConsumable(size_t index) const;
  void writeConsumable(size_t index, uint8_t value);
  void setAllConsumables(uint8_t value);
  void clearConsumables();
  size_t addAllTreasures(uint8_t level, uint8_t experience);
};
enum class VariableKind { Boolean, Integer, Pointer };
struct VariableCell {
  size_t index = 0;
  uint32_t address = 0;
  uint32_t value = 0;
};
class VariableService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;
  const VariableLayout &layout(VariableKind kind) const;

public:
  VariableService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  size_t recordCount(VariableKind kind) const;
  size_t browseCount(VariableKind kind) const;
  size_t writableCount(VariableKind kind) const;
  bool readBool(size_t index) const;
  void writeBool(size_t index, bool value);
  void writeBools(const std::vector<std::pair<size_t, bool>> &values);
  int32_t readInt(size_t index) const;
  void writeInt(size_t index, int32_t value);
  uint32_t readPointer(size_t index) const;
  void writePointer(size_t index, uint32_t value);
  std::vector<VariableCell> readPage(VariableKind kind, size_t start,
                                     size_t count) const;
};
class PatchManager {
  std::shared_ptr<MemorySession> session_;

public:
  explicit PatchManager(std::shared_ptr<MemorySession> s)
      : session_(std::move(s)) {}
  void apply(uint32_t address, const Bytes &expected, const Bytes &replacement);
  void restore(uint32_t address, const Bytes &expected, const Bytes &original);
};
class LegacyAutoReturnPatch {
public:
  static Bytes buildStub(const Bytes &original, const EngineProfile &profile);
  static void set(const std::shared_ptr<MemorySession> &session,
                  const EngineProfile &profile, bool enabled);
};
struct EffectRecord {
  std::array<uint16_t, 4> characters{};
  std::array<uint8_t, 4> characterValues{};
  std::array<uint8_t, 2> jobs{};
  std::array<uint8_t, 2> jobValues{};
};
class EffectService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;
  uint32_t table() const;

public:
  EffectService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  EffectRecord read(size_t id) const;
  void write(size_t id, const EffectRecord &row);
};
struct ExclusiveEffectRecord {
  std::array<uint16_t, 2> persons{};
  std::array<uint8_t, 2> personItems{};
  std::array<uint8_t, 2> personValues{};
  std::array<std::array<uint8_t, 3>, 2> setItems{};
  std::array<uint8_t, 2> setValues{};
};
class ExclusiveEffectService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;

public:
  ExclusiveEffectService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  ExclusiveEffectRecord read(size_t id) const;
  void write(size_t id, const ExclusiveEffectRecord &row);
};
struct FatalRecord {
  std::array<uint16_t, 5> persons{};
  std::array<uint8_t, 5> levels{};
  uint8_t value = 0;
};
class FatalService {
  std::shared_ptr<MemorySession> session_;
  EngineProfile profile_;
  uint32_t table() const;

public:
  FatalService(std::shared_ptr<MemorySession> s, EngineProfile p)
      : session_(std::move(s)), profile_(std::move(p)) {}
  FatalRecord read(size_t id) const;
  void write(size_t id, const FatalRecord &row);
};
class DiyService {
  std::shared_ptr<MemorySession> session_;

public:
  explicit DiyService(std::shared_ptr<MemorySession> s)
      : session_(std::move(s)) {}
  void write(uint32_t address, uint32_t value, size_t width);
};
} // namespace banshou
