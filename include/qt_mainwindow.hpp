#pragma once

#include "diy.hpp"
#include "remote.hpp"
#include "session.hpp"
#include <QMainWindow>
#include <QPoint>
#include <QTimer>
#include <array>
#include <chrono>
#include <memory>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

namespace banshou {

// 仓库行文本：0xFF 显示「空」，超出物品表范围显示「无效编号：N」，
// 其余显示「ID：名称」。独立成纯函数，便于 UI 契约测试直接断言。
QString warehouseRowText(uint16_t item, uint32_t itemCount,
                         const QString &name);

struct LockedBattleUnit {
  size_t slot = 0;
  uint16_t dataId = 0xffff;
  uint32_t hp = 0;
  uint32_t mp = 0;
};

class QtMainWindow final : public QMainWindow {
public:
  explicit QtMainWindow(QWidget *parent = nullptr);
  ~QtMainWindow() override;

  Ui::MainWindow &uiForTests() const { return *ui_; }
  int currentPageForTests() const { return currentPage_; }
  void showPageForTests(int page) { showPage(page); }
  bool attachPid(uint32_t pid, bool readOnly = false);
  Json diagnosticSnapshot() const;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void closeEvent(QCloseEvent *event) override;
  void changeEvent(QEvent *event) override;

private:
  std::unique_ptr<Ui::MainWindow> ui_;
  SessionController session_;
  uint64_t uiGeneration_ = 0;
  int currentPage_ = 0;
  bool selecting_ = false;
  HWND candidateWindow_ = nullptr;
  std::array<bool, 9> controlState_{};
  std::vector<LockedBattleUnit> locked_;
  std::map<std::pair<size_t, uint16_t>, std::chrono::steady_clock::time_point>
      autoReviveAttempts_;
  QTimer lockTimer_;
  QTimer autoLifeTimer_;
  std::shared_ptr<MemorySession> remoteSession_;
  std::unique_ptr<RemoteServices> remoteServices_;
  std::shared_ptr<MemorySession> catalogSession_;
  mutable std::unordered_map<uint16_t, QString> personNameCache_;
  mutable std::unordered_map<uint16_t, QString> itemNameCache_;
  DiyCsv diy_;
  std::wstring diyPath_;
  std::vector<DiyExpandedRow> diyVisible_;
  size_t diySection_ = 0;
  size_t diyOffset_ = 0;
  std::array<QLineEdit *, 4> conditionDigits_{};
  std::array<QPushButton *, 3> variableSave_{};

  void createPythonOnlyControls();
  void connectUi();
  void setInitialState();
  void showPage(int page);
  void setStatus(const QString &message);
  void setStatus(const std::string &message);
  void guarded(const QString &operation, const std::function<void()> &work);
  void beginWindowSelection();
  void cancelWindowSelection(const QString &status = QString());
  void finishWindowSelection();
  int selectedVersionHint() const;
  void onSessionState(SessionState state, uint64_t generation,
                      const std::string &error);
  void resetSessionUi();
  void stopRecurringOperations();
  void releaseControlPatches();
  RemoteServices &remoteFor(const std::shared_ptr<MemorySession> &session,
                            const EngineProfile &profile);

  void initializeAttachedUi();
  void populateSharedCatalogs();
  QString gameText(const std::shared_ptr<MemorySession> &session,
                   uint32_t address, size_t width) const;
  QString personName(uint16_t id) const;
  QString itemName(uint16_t id) const;
  uint32_t unsignedText(const QString &text, uint32_t maximum,
                        const char *field) const;
  int32_t signedText(const QString &text, const char *field) const;
  uint32_t editUnsigned(const QTextEdit *edit, uint32_t maximum,
                        const char *field) const;
  void setText(QTextEdit *edit, uint64_t value) const;
  size_t selectedBattleSlot() const;

  void refreshCurrentPage();
  void refreshPeople();
  void loadPerson();
  void savePerson();
  void recalculatePerson();
  void refreshBattle(bool preserveSelection = false);
  void loadBattle();
  void saveBattle();
  void refreshBattleStrategy();
  void connectBattlePerson();
  void reviveBattleUnit();
  void killBattleArea();
  void addBattleLock();
  void removeBattleLock();
  void runBattleLocks();
  void runAutoRevive();
  void applyGroupBuffs();
  void refreshWarehouse();
  void loadWarehouseRecord();
  void loadConsumable();
  void saveWarehouse();
  void warehouseBulk(int action);
  void refreshEffects();
  void loadEffect();
  void saveEffect();
  void updateEffectLayout();
  void refreshVariables();
  void saveVariable(int kind);
  void setupBooleanActions();
  void saveBooleanPage();
  void applyCapabilities();
  void exportBooleanArchive();
  void importBooleanArchive();
  void loadDiy();
  void refreshDiy();
  void advanceDiyWindow(int scrollValue);
  void saveDiyValue();
  void refreshFatal();
  void loadFatal();
  void saveFatal();
  void toggleControl(int index);
};

int runWindow(int argc, wchar_t **argv);

} // namespace banshou
