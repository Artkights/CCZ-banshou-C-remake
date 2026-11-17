#pragma once

#include "game.hpp"
#include "win32_mainwindow.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace banshou {

// Business orchestration shared by the native window and its tests.
class Win32Business {
public:
  explicit Win32Business(Win32MainWindow &window) : window_(window) {}

  std::shared_ptr<MemorySession> session() const { return window_.session(); }
  EngineProfile profile() const { return window_.profile(); }

  void populateCatalogs();
  void refreshPeople();
  void loadPerson();
  void savePerson();
  void recalculatePerson();
  void connectBattlePerson();

  void refreshBattle(bool full = false);
  void loadBattle();
  void saveBattle();
  void reviveBattleUnit();
  void killBattleArea();
  void addBattleLock();
  void removeBattleLock();
  void runBattleLocks();
  void runAutoRevive();
  void refreshBattleStrategy();

  void refreshWarehouse();
  void loadWarehouseRecord();
  void loadConsumable();
  void saveWarehouse();
  void warehouseBulk(int action);

  void refreshEffects();
  void updateEffectLayout();
  void loadEffect();
  void saveEffect();

  void refreshVariables();
  void saveVariable(int kind);
  void saveBooleanPage();
  void exportBooleanArchive();
  void importBooleanArchive();
  void showBooleanMenu(HWND controlWindow, POINT screenPoint);

  void loadDiy();
  void refreshDiy();
  void saveDiyValue();
  void openDiy();
  void saveDiy();
  void advanceDiyWindow();

  void refreshFatal();
  void loadFatal();
  void saveFatal();

  void refreshCurrentPage(int page);

private:
  Win32MainWindow &window_;
  std::unordered_map<std::wstring, uint64_t> personIdByName_;

  std::wstring nativeItemName(uint16_t id) const;
  std::wstring nativePersonName(uint16_t id) const;
};

std::wstring warehouseRowText(uint16_t item, uint32_t itemCount,
                              const std::wstring &name);

}  // namespace banshou
