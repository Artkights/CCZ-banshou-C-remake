#include "qt_mainwindow.hpp"
#include "ui_mainwindow.h"
#include "variables.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolBar>
#include <algorithm>
#include <array>
#include <climits>
#include <optional>
#include <sstream>

#ifdef QT_STATIC
#include <QtPlugin>
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#ifdef BANSHOU_QT_STATIC_TESTS
Q_IMPORT_PLUGIN(QOffscreenIntegrationPlugin)
#endif
#endif

namespace banshou {
namespace {
const char *const kPatchKeys[] = {
    "control_friendly",        "control_all_factions", "control_no_ai",
    "control_move_after_wait", "control_move_255",     "control_pass_through",
    "control_auto_return"};

QString fromUtf8(const std::string &value) {
  return QString::fromUtf8(value.data(), int(value.size()));
}

QString decodeCodePage(const uint8_t *data, size_t length, UINT codePage) {
  if (!data || length == 0 || length > INT_MAX)
    return {};
  auto end = std::find(data, data + length, uint8_t(0));
  const auto size = int(end - data);
  if (size == 0)
    return {};
  const auto count = MultiByteToWideChar(
      codePage, MB_ERR_INVALID_CHARS, reinterpret_cast<LPCCH>(data), size,
      nullptr, 0);
  if (!count)
    return {};
  std::wstring text(size_t(count), L'\0');
  if (!MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                           reinterpret_cast<LPCCH>(data), size, text.data(),
                           count))
    return {};
  return QString::fromWCharArray(text.data(), count);
}

QString legacyTableText(const Bytes &bytes, size_t offset, size_t width) {
  if (offset > bytes.size() || width > bytes.size() - offset)
    return {};
  for (UINT codePage : {UINT(936), UINT(54936)}) {
    auto text = decodeCodePage(bytes.data() + offset, width, codePage);
    if (!text.isEmpty())
      return text;
  }
  return QStringLiteral("非法字符");
}

QString hexAddress(uint32_t value) {
  return QStringLiteral("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

template <typename T> void selectData(QComboBox *combo, T value) {
  auto index = -1;
  const auto expected = qulonglong(value);
  for (int row = 0; row < combo->count(); ++row) {
    bool ok = false;
    const auto actual = combo->itemData(row).toULongLong(&ok);
    if (ok && actual == expected) {
      index = row;
      break;
    }
  }
  if (index < 0 && combo->count())
    index = combo->count() - 1;
  combo->setCurrentIndex(index);
}

std::array<QWidget *, 8> pages(Ui::MainWindow &ui) {
  return {ui.widget_1, ui.widget_2, ui.widget_3, ui.widget_4,
          ui.widget_5, ui.widget_6, ui.widget_7, ui.widget_8};
}

std::array<QTextEdit *, 38> personEdits(Ui::MainWindow &ui) {
  return {ui.data_input_1,  ui.data_input_2,  ui.data_input_3,
          ui.data_input_4,  ui.data_input_5,  ui.data_input_6,
          ui.data_input_7,  ui.data_input_8,  ui.data_input_9,
          ui.data_input_10, nullptr,          ui.data_input_12,
          ui.data_input_13, ui.data_input_14, ui.data_input_15,
          ui.data_input_16, ui.data_input_17, ui.data_input_18,
          ui.data_input_19, ui.data_input_20, ui.data_input_21,
          ui.data_input_22, ui.data_input_23, ui.data_input_24,
          ui.data_input_25, ui.data_input_26, ui.data_input_27,
          ui.data_input_28, ui.data_input_29, ui.data_input_30,
          nullptr,          nullptr,          ui.data_input_33,
          ui.data_input_34, nullptr,          ui.data_input_36,
          ui.data_input_37, nullptr};
}

uint8_t comboByte(const QComboBox *combo, uint8_t empty = 0xff) {
  if (combo->currentIndex() < 0)
    return empty;
  auto data = combo->currentData();
  if (!data.isValid())
    return combo->currentIndex() == combo->count() - 1
               ? empty
               : uint8_t(combo->currentIndex());
  auto value = data.toUInt();
  return value > 0xff ? empty : uint8_t(value);
}
} // namespace

QtMainWindow::QtMainWindow(QWidget *parent)
    : QMainWindow(parent), ui_(std::make_unique<Ui::MainWindow>()) {
  ui_->setupUi(this);
  // 窗口级默认字体：未显式声明字体的控件继承系统字体（125% 缩放下等效 12pt），
  // 与设计基线 9pt 混排会让同一页字号参差、文字被裁。显式设置过字体的控件不受影响。
  const QFont baseFont(QStringLiteral("微软雅黑"), 9);
  setFont(baseFont);
  if (ui_->centralwidget)
    ui_->centralwidget->setFont(baseFont);
  createPythonOnlyControls();
  setInitialState();
  setupBooleanActions();
  connectUi();
  session_.onState = [this](SessionState state, uint64_t generation,
                            const std::string &error) {
    QMetaObject::invokeMethod(
        this,
        [this, state, generation, error] {
          onSessionState(state, generation, error);
        },
        Qt::QueuedConnection);
  };
}

QtMainWindow::~QtMainWindow() {
  stopRecurringOperations();
  session_.detach();
}

void QtMainWindow::createPythonOnlyControls() {
  QFont font(QStringLiteral("微软雅黑"), 9);
  const std::array<QRect, 4> geometries = {
      QRect(185, 170, 25, 25), QRect(255, 170, 25, 25),
      QRect(185, 200, 25, 25), QRect(255, 200, 25, 25)};
  for (size_t i = 0; i < conditionDigits_.size(); ++i) {
    auto *edit = new QLineEdit(ui_->widget_3);
    edit->setObjectName(QStringLiteral("war_input_%1").arg(51 + int(i)));
    edit->setGeometry(geometries[i]);
    edit->setFont(font);
    edit->setStyleSheet(QStringLiteral(
        "background-color:rgb(255,255,255);color:rgb(0,0,0);"));
    edit->setText(QStringLiteral("0"));
    // 默认隐藏：这 4 个输入框与状态复选框重叠，只在引擎使用扩展状态位时
    // 由 loadBattle() 切换显示，避免未附加时挡住复选框。
    edit->setVisible(false);
    conditionDigits_[i] = edit;
  }
  const std::array<QRect, 3> saveGeometry = {
      QRect(170, 20, 50, 25), QRect(390, 20, 50, 25),
      QRect(610, 20, 50, 25)};
  for (size_t i = 0; i < variableSave_.size(); ++i) {
    auto *button = new QPushButton(QStringLiteral("保存"), ui_->widget_6);
    button->setObjectName(QStringLiteral("var_save_%1").arg(int(i + 1)));
    button->setGeometry(saveGeometry[i]);
    button->setStyleSheet(QStringLiteral(
        "background-color:rgb(255,255,255);color:rgb(0,0,0);"));
    variableSave_[i] = button;
  }
}

void QtMainWindow::setInitialState() {
  setFixedSize(700, 530);
  setFocusPolicy(Qt::ClickFocus);
  setContextMenuPolicy(Qt::NoContextMenu);
  ui_->version_1->setChecked(true);
  // 初始化天赋页布局，避免首次刷新前扩展态控件与基础态控件互相重叠。
  updateEffectLayout();
  ui_->label_pivot->setPixmap(QPixmap(QStringLiteral(":/banshou/target.png")));
  ui_->toolBar->setEnabled(false);
  ui_->widget_0->hide();
  showPage(0);
  ui_->widget_1->setEnabled(false);
  ui_->var_input_1->setText(QStringLiteral("0"));
  ui_->var_input_2->setText(QStringLiteral("0"));
  ui_->var_input_3->setText(QStringLiteral("0"));
  ui_->war_input_47->setText(QStringLiteral("0"));
  ui_->war_input_48->setText(QStringLiteral("0"));
  ui_->war_input_49->setText(QStringLiteral("255"));
  ui_->war_input_50->setText(QStringLiteral("255"));
  ui_->war_kind_1->setChecked(true);
  ui_->listview_var_1->setColumnWidth(0, 60);
  ui_->listview_var_1->setColumnWidth(1, 55);
  ui_->listview_var_2->setColumnWidth(0, 45);
  ui_->listview_var_2->setColumnWidth(1, 75);
  ui_->listview_var_2->setColumnWidth(2, 65);
  ui_->listview_var_3->setColumnWidth(0, 45);
  ui_->listview_var_3->setColumnWidth(1, 75);
  ui_->listview_diy->setColumnWidth(0, 100);
  ui_->listview_diy->setColumnWidth(1, 80);
  ui_->listview_diy->setColumnWidth(2, 90);
  ui_->listview_diy->setColumnWidth(3, 70);
  ui_->listview_diy->setColumnWidth(4, 45);
  ui_->listview_diy->setColumnWidth(5, 230);
  ui_->label_pivot->installEventFilter(this);
  lockTimer_.setInterval(1000);
  autoLifeTimer_.setInterval(1000);
}

void QtMainWindow::connectUi() {
  const std::array<QAction *, 8> actions = {
      ui_->action_C, ui_->action_M, ui_->action_B, ui_->action_W,
      ui_->action_T, ui_->action_V, ui_->action_D, ui_->action_K};
  for (size_t i = 0; i < actions.size(); ++i)
    connect(actions[i], &QAction::triggered, this,
            [this, i] { showPage(int(i)); });

  const std::array<QCheckBox *, 9> controls = {
      ui_->checkBox_1, ui_->checkBox_2, ui_->checkBox_3,
      ui_->checkBox_4, ui_->checkBox_5, ui_->checkBox_6,
      ui_->checkBox_7, ui_->checkBox_8, ui_->checkBox_9};
  for (size_t i = 0; i < controls.size(); ++i)
    connect(controls[i], &QCheckBox::clicked, this,
            [this, i] { guarded(QStringLiteral("控制项"), [this, i] {
               toggleControl(int(i));
             }); });

  connect(ui_->listview_data, &QListWidget::itemSelectionChanged, this,
          [this] { guarded(QStringLiteral("读取人物"), [this] { loadPerson(); }); });
  connect(ui_->data_save, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("保存人物"), [this] { savePerson(); }); });
  connect(ui_->data_recal, &QPushButton::clicked, this, [this] {
    guarded(QStringLiteral("能力重新计算"),
            [this] { recalculatePerson(); });
  });

  for (auto *button : {ui_->war_kind_1, ui_->war_kind_2, ui_->war_kind_3})
    connect(button, &QRadioButton::clicked, this, [this] {
      guarded(QStringLiteral("刷新战场"), [this] { refreshBattle(); });
    });
  connect(ui_->listview_war, &QListWidget::itemSelectionChanged, this,
          [this] { guarded(QStringLiteral("读取战场"), [this] { loadBattle(); }); });
  connect(ui_->war_input_11,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this] { refreshBattleStrategy(); });
  connect(ui_->war_save, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("保存战场"), [this] { saveBattle(); }); });
  connect(ui_->war_connect, &QPushButton::clicked, this, [this] {
    guarded(QStringLiteral("连接人物"), [this] { connectBattlePerson(); });
  });
  connect(ui_->war_life, &QPushButton::clicked, this, [this] {
    guarded(QStringLiteral("战场复活"), [this] { reviveBattleUnit(); });
  });
  connect(ui_->war_kill, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("区域灭敌"), [this] { killBattleArea(); }); });
  connect(ui_->war_lockadd, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("增加锁定"), [this] { addBattleLock(); }); });
  connect(ui_->war_locksub, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("移除锁定"), [this] { removeBattleLock(); }); });

  connect(ui_->listview_item, &QListWidget::itemSelectionChanged, this,
          [this] { guarded(QStringLiteral("读取仓库"), [this] { loadWarehouseRecord(); }); });
  connect(ui_->listview_item_2, &QListWidget::itemSelectionChanged, this,
          [this] { guarded(QStringLiteral("读取道具"), [this] { loadConsumable(); }); });
  connect(ui_->item_save, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("保存仓库"), [this] { saveWarehouse(); }); });
  connect(ui_->item_all_1, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("全宝物"), [this] { warehouseBulk(1); }); });
  connect(ui_->item_all_2, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("全道具"), [this] { warehouseBulk(2); }); });
  connect(ui_->item_all_3, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("清空仓库"), [this] { warehouseBulk(3); }); });

  connect(ui_->listview_item_3, &QListWidget::itemSelectionChanged, this,
          [this] { guarded(QStringLiteral("读取天赋"), [this] { loadEffect(); }); });
  connect(ui_->item_save_2, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("保存天赋"), [this] { saveEffect(); }); });

  for (auto *button : {ui_->var_input_4, ui_->var_input_5, ui_->var_input_6})
    connect(button, &QPushButton::clicked, this,
            [this] { guarded(QStringLiteral("刷新变量"), [this] { refreshVariables(); }); });
  for (size_t i = 0; i < variableSave_.size(); ++i)
    connect(variableSave_[i], &QPushButton::clicked, this, [this, i] {
      guarded(QStringLiteral("保存变量"), [this, i] { saveVariable(int(i)); });
    });
  connect(ui_->diy_input_2, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("保存自定义值"), [this] { saveDiyValue(); }); });
  connect(ui_->diy_input_4,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int index) {
            if (index >= 0) {
              diySection_ = size_t(index);
              diyOffset_ = 0;
              guarded(QStringLiteral("刷新自定义"), [this] { refreshDiy(); });
            }
          });
  connect(ui_->listview_diy->verticalScrollBar(), &QScrollBar::valueChanged,
          this, [this](int value) { advanceDiyWindow(value); });
  connect(ui_->listview_mk, &QListWidget::itemSelectionChanged, this,
          [this] { guarded(QStringLiteral("读取必杀"), [this] { loadFatal(); }); });
  connect(ui_->item_save_4, &QPushButton::clicked, this,
          [this] { guarded(QStringLiteral("保存必杀"), [this] { saveFatal(); }); });

  connect(&lockTimer_, &QTimer::timeout, this,
          [this] { guarded(QStringLiteral("锁定"), [this] { runBattleLocks(); }); });
  connect(&autoLifeTimer_, &QTimer::timeout, this, [this] {
    try {
      runAutoRevive();
    } catch (const std::exception &error) {
      autoLifeTimer_.stop();
      ui_->checkBox_9->setChecked(false);
      setStatus(QStringLiteral("自动复活失败：") + fromUtf8(error.what()));
    } catch (...) {
      autoLifeTimer_.stop();
      ui_->checkBox_9->setChecked(false);
      setStatus(QStringLiteral("自动复活失败：未知异常"));
    }
  });
}

void QtMainWindow::guarded(const QString &operation,
                           const std::function<void()> &work) {
  try {
    work();
  } catch (const std::exception &error) {
    setStatus(operation + QStringLiteral("失败：") + fromUtf8(error.what()));
  } catch (...) {
    setStatus(operation + QStringLiteral("失败：未知异常"));
  }
}

void QtMainWindow::showPage(int page) {
  if (page < 0 || page >= 8)
    return;
  const auto allPages = pages(*ui_);
  for (size_t i = 0; i < allPages.size(); ++i)
    allPages[i]->setVisible(int(i) == page);
  static const std::array<const char *, 8> names = {
      "控制", "人物", "战场", "仓库", "天赋", "变量", "自定", "必杀"};
  currentPage_ = page;
  ui_->label_title->setText(QString::fromUtf8(names[size_t(page)]));
  if (session_.state() == SessionState::Attached)
    guarded(QStringLiteral("刷新页面"), [this] { refreshCurrentPage(); });
}

void QtMainWindow::setStatus(const QString &message) {
  const auto visible = ui_->label_window2->fontMetrics().elidedText(
      message, Qt::ElideRight, ui_->label_window2->width() - 8);
  ui_->label_window2->setText(visible);
  ui_->label_window2->setToolTip(message);
}

void QtMainWindow::setStatus(const std::string &message) {
  setStatus(fromUtf8(message));
}

bool QtMainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (watched == ui_->label_pivot &&
      event->type() == QEvent::MouseButtonPress) {
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() == Qt::LeftButton) {
      beginWindowSelection();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void QtMainWindow::beginWindowSelection() {
  if (selecting_)
    return;
  selecting_ = true;
  candidateWindow_ = nullptr;
  ui_->label_pivot->setPixmap(QPixmap());
  auto cursor = LoadCursorW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(102));
  if (cursor)
    SetCursor(cursor);
  grabMouse();
  setStatus(QStringLiteral("请将准星拖至游戏窗口，按 Esc 取消"));
}

void QtMainWindow::cancelWindowSelection(const QString &status) {
  if (selecting_)
    releaseMouse();
  selecting_ = false;
  candidateWindow_ = nullptr;
  SetCursor(LoadCursorW(nullptr, IDC_ARROW));
  ui_->label_pivot->setPixmap(QPixmap(QStringLiteral(":/banshou/target.png")));
  if (!status.isEmpty())
    setStatus(status);
}

void QtMainWindow::mouseMoveEvent(QMouseEvent *event) {
  if (selecting_) {
    POINT point{};
    GetCursorPos(&point);
    auto target = GetAncestor(WindowFromPoint(point), GA_ROOT);
    if (target && target != reinterpret_cast<HWND>(winId())) {
      candidateWindow_ = target;
      std::array<wchar_t, 512> title{}, className{};
      GetWindowTextW(target, title.data(), int(title.size()));
      GetClassNameW(target, className.data(), int(className.size()));
      ui_->label_window1->setText(QString::fromWCharArray(title.data()));
      setStatus(QString::fromWCharArray(className.data()));
    }
  }
  QMainWindow::mouseMoveEvent(event);
}

void QtMainWindow::mouseReleaseEvent(QMouseEvent *event) {
  if (selecting_ && event->button() == Qt::LeftButton) {
    finishWindowSelection();
    return;
  }
  QMainWindow::mouseReleaseEvent(event);
}

void QtMainWindow::keyPressEvent(QKeyEvent *event) {
  if (selecting_ && event->key() == Qt::Key_Escape) {
    cancelWindowSelection(QStringLiteral("未附加"));
    return;
  }
  QMainWindow::keyPressEvent(event);
}

void QtMainWindow::finishWindowSelection() {
  auto target = candidateWindow_;
  cancelWindowSelection();
  if (!target || target == reinterpret_cast<HWND>(winId())) {
    setStatus(QStringLiteral("未选择有效的外部窗口"));
    return;
  }
  releaseControlPatches();
  stopRecurringOperations();
  auto generation = session_.attach(target, selectedVersionHint());
  uiGeneration_ = std::max<uint64_t>(uiGeneration_, generation);
}

int QtMainWindow::selectedVersionHint() const {
  if (ui_->version_1->isChecked())
    return -1;
  if (ui_->version_5->isChecked())
    return int(EngineVersion::V66);
  if (ui_->version_6->isChecked())
    return int(EngineVersion::V65);
  if (ui_->version_0->isChecked())
    return int(EngineVersion::V64);
  if (ui_->version_2->isChecked())
    return int(EngineVersion::V63);
  if (ui_->version_3->isChecked())
    return int(EngineVersion::V62);
  return int(EngineVersion::V61);
}

void QtMainWindow::onSessionState(SessionState state, uint64_t generation,
                                  const std::string &error) {
  if (generation < uiGeneration_)
    return;
  uiGeneration_ = generation;
  if (state == SessionState::Attached) {
    setStatus(QStringLiteral("已附加"));
    try {
      initializeAttachedUi();
    } catch (const std::exception &error) {
      setStatus(QStringLiteral("初始化失败：") + fromUtf8(error.what()));
    } catch (...) {
      setStatus(QStringLiteral("初始化失败：未知异常"));
    }
  } else if (state == SessionState::Attaching) {
    setStatus(QStringLiteral("附加中"));
  } else if (state == SessionState::Detaching) {
    setStatus(QStringLiteral("解绑中"));
  } else if (state == SessionState::Error) {
    stopRecurringOperations();
    resetSessionUi();
    setStatus(QStringLiteral("附加失败：") + fromUtf8(error));
  } else {
    stopRecurringOperations();
    resetSessionUi();
    setStatus(QStringLiteral("未附加"));
  }
}

void QtMainWindow::resetSessionUi() {
  ui_->toolBar->setEnabled(false);
  ui_->widget_1->setEnabled(false);
  catalogSession_.reset();
  personNameCache_.clear();
  itemNameCache_.clear();
  for (auto *list : {ui_->listview_data, ui_->listview_war,
                     ui_->listview_war_2, ui_->listview_item,
                     ui_->listview_item_2, ui_->listview_item_3,
                     ui_->listview_mk}) {
    const QSignalBlocker blocker(list);
    list->clear();
  }
  for (auto *table : {ui_->listview_var_1, ui_->listview_var_2,
                      ui_->listview_var_3, ui_->listview_diy}) {
    const QSignalBlocker blocker(table);
    table->setRowCount(0);
  }
  applyCapabilities();
}

void QtMainWindow::stopRecurringOperations() {
  lockTimer_.stop();
  autoLifeTimer_.stop();
  locked_.clear();
  autoReviveAttempts_.clear();
  ui_->listview_war_2->clear();
  if (ui_->checkBox_9)
    ui_->checkBox_9->setChecked(false);
  remoteServices_.reset();
  remoteSession_.reset();
}

void QtMainWindow::releaseControlPatches() {
  if (!session_.session())
    return;
  const std::array<QCheckBox *, 7> controls = {
      ui_->checkBox_1, ui_->checkBox_2, ui_->checkBox_3, ui_->checkBox_4,
      ui_->checkBox_5, ui_->checkBox_6, ui_->checkBox_7};
  for (size_t index = 0; index < controls.size(); ++index) {
    if (!controls[index]->isChecked())
      continue;
    try {
      controls[index]->setChecked(false);
      toggleControl(int(index));
    } catch (const std::exception &error) {
      log(std::string("恢复控制补丁失败：") + error.what());
    } catch (...) {
      log("恢复控制补丁失败：未知异常");
    }
  }
}

RemoteServices &
QtMainWindow::remoteFor(const std::shared_ptr<MemorySession> &session,
                        const EngineProfile &profile) {
  if (!remoteServices_ || remoteSession_.get() != session.get()) {
    remoteServices_ = std::make_unique<RemoteServices>(session, profile);
    remoteSession_ = session;
  }
  return *remoteServices_;
}

void QtMainWindow::closeEvent(QCloseEvent *event) {
  releaseControlPatches();
  stopRecurringOperations();
  session_.detach();
  event->accept();
}

void QtMainWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::ActivationChange && isActiveWindow() &&
      session_.state() == SessionState::Attached)
    guarded(QStringLiteral("刷新页面"), [this] { refreshCurrentPage(); });
}

uint32_t QtMainWindow::unsignedText(const QString &text, uint32_t maximum,
                                    const char *field) const {
  auto value = text.trimmed();
  if (value.isEmpty())
    throw Error(std::string(field) + "不能为空");
  bool ok = false;
  auto parsed = value.toULongLong(&ok, 0);
  if (!ok || parsed > maximum)
    throw Error(std::string(field) + "数值越界");
  return uint32_t(parsed);
}

int32_t QtMainWindow::signedText(const QString &text, const char *field) const {
  auto value = text.trimmed();
  if (value.isEmpty())
    throw Error(std::string(field) + "不能为空");
  bool ok = false;
  auto parsed = value.toLongLong(&ok, 0);
  if (!ok || parsed < INT32_MIN || parsed > INT32_MAX)
    throw Error(std::string(field) + "数值越界");
  return int32_t(parsed);
}

uint32_t QtMainWindow::editUnsigned(const QTextEdit *edit, uint32_t maximum,
                                    const char *field) const {
  return unsignedText(edit->toPlainText(), maximum, field);
}

void QtMainWindow::setText(QTextEdit *edit, uint64_t value) const {
  edit->setText(QString::number(qulonglong(value)));
}

QString QtMainWindow::gameText(const std::shared_ptr<MemorySession> &session,
                               uint32_t address, size_t width) const {
  auto bytes = session->read(address, width);
  for (UINT codePage : {UINT(54936), UINT(936)}) {
    auto text = decodeCodePage(bytes.data(), bytes.size(), codePage);
    if (!text.isEmpty())
      return text;
  }
  return QStringLiteral("非法字符");
}

QString QtMainWindow::personName(uint16_t id) const {
  if (const auto cached = personNameCache_.find(id);
      cached != personNameCache_.end())
    return cached->second;
  auto session = session_.session();
  if (!session)
    return QString::number(id);
  try {
    auto profile = session_.profile();
    auto base = session->u(profile.address("person_pointer", 0x004CEA00), 4);
    auto name = gameText(session,
                         checkedAddress(base, uint64_t(id) * 0x48 + 8, 8), 8);
    if (name.isEmpty())
      name = QStringLiteral("人物%1").arg(id);
    personNameCache_[id] = name;
    return name;
  } catch (...) {
    auto name = QStringLiteral("人物%1").arg(id);
    personNameCache_[id] = name;
    return name;
  }
}

QString QtMainWindow::itemName(uint16_t id) const {
  if (const auto cached = itemNameCache_.find(id);
      cached != itemNameCache_.end())
    return cached->second;
  auto session = session_.session();
  if (!session)
    return QStringLiteral("物品%1").arg(id);
  auto profile = session_.profile();
  auto table = profile.address("item_table", 0);
  if (!table) {
    auto name = QStringLiteral("物品%1").arg(id);
    itemNameCache_[id] = name;
    return name;
  }
  try {
    auto name = gameText(session,
                         checkedAddress(table, uint64_t(id) * 25,
                                        profile.limit("item_name_size", 12)),
                         profile.limit("item_name_size", 12));
    if (name.isEmpty())
      name = QStringLiteral("物品%1").arg(id);
    itemNameCache_[id] = name;
    return name;
  } catch (...) {
    auto name = QStringLiteral("物品%1").arg(id);
    itemNameCache_[id] = name;
    return name;
  }
}

QString warehouseRowText(uint16_t item, uint32_t itemCount,
                         const QString &name) {
  if (item == 0xff)
    return QStringLiteral("空");
  if (item >= itemCount)
    return QStringLiteral("无效编号：%1").arg(item);
  return QStringLiteral("%1：%2").arg(item).arg(name);
}

void QtMainWindow::initializeAttachedUi() {
  ui_->toolBar->setEnabled(true);
  ui_->widget_1->setEnabled(true);
  controlState_.fill(false);
  for (auto *box : {ui_->checkBox_1, ui_->checkBox_2, ui_->checkBox_3,
                    ui_->checkBox_4, ui_->checkBox_5, ui_->checkBox_6,
                    ui_->checkBox_7, ui_->checkBox_8, ui_->checkBox_9})
    box->setChecked(false);
  stopRecurringOperations();
  populateSharedCatalogs();
  // DIY.csv 三级查找：exe 目录 → 用户数据目录 → 当前工作目录。首次运行会把
  // 随包的 DIY.csv 复制到用户目录，升级替换 exe 时不会覆盖用户自己的定义。
  const auto appDiy = QDir(QCoreApplication::applicationDirPath())
                          .absoluteFilePath(QStringLiteral("DIY.csv"));
  const auto dataDir =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QString userDiy;
  if (!dataDir.isEmpty()) {
    QDir().mkpath(dataDir);
    userDiy = QDir(dataDir).absoluteFilePath(QStringLiteral("DIY.csv"));
  }
  const auto cwdDiy =
      QDir::current().absoluteFilePath(QStringLiteral("DIY.csv"));
  QString chosen;
  for (const auto &candidate : {appDiy, userDiy, cwdDiy}) {
    if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
      chosen = candidate;
      break;
    }
  }
  if (chosen.isEmpty()) {
    diyPath_.clear();
  } else {
    if (!userDiy.isEmpty() && chosen != userDiy &&
        !QFileInfo::exists(userDiy))
      QFile::copy(chosen, userDiy);
    const auto effective =
        (!userDiy.isEmpty() && QFileInfo::exists(userDiy)) ? userDiy : chosen;
    diyPath_ = effective.toStdWString();
    diy_.load(diyPath_);
  }
  applyCapabilities();
  refreshCurrentPage();
}

void QtMainWindow::populateSharedCatalogs() {
  auto session = session_.session();
  if (!session)
    return;
  if (catalogSession_.get() != session.get()) {
    catalogSession_ = session;
    personNameCache_.clear();
    itemNameCache_.clear();
  }
  auto profile = session_.profile();
  const auto personCount = profile.limit("person_count", 1024);
  const auto itemCount =
      std::min<uint32_t>(profile.limit("item_count", 255), 512);
  const auto equipmentCount =
      std::min<uint32_t>(profile.limit("equipment_count", itemCount), 255);

  ui_->listview_data->clear();
  std::vector<QString> personNames;
  personNames.reserve(personCount);
  // Names are fixed-width records. Read the complete table once during an
  // attach instead of issuing one cross-process read per list entry.
  try {
    const auto personBase =
        session->u(profile.address("person_pointer", 0x004CEA00), 4);
    const auto personBytes =
        session->readActual(personBase, size_t(personCount) * 0x48);
    for (uint32_t i = 0; i < personCount; ++i) {
      auto name = decodeCodePage(personBytes.data() + size_t(i) * 0x48 + 8,
                                 8, 54936);
      if (name.isEmpty())
        name = decodeCodePage(personBytes.data() + size_t(i) * 0x48 + 8, 8,
                              936);
      if (!name.isEmpty())
        personNameCache_[uint16_t(i)] = name;
    }
  } catch (...) {
    // Some protected/packed variants do not expose a contiguous table. The
    // per-record reader below remains the compatibility fallback.
  }
  for (uint32_t i = 0; i < personCount; ++i) {
    auto name = personName(uint16_t(i));
    personNames.push_back(name);
    auto *item = new QListWidgetItem(QStringLiteral("%1:%2").arg(i).arg(name));
    item->setData(Qt::UserRole, i);
    ui_->listview_data->addItem(item);
  }

  const std::array<QComboBox *, 12> personCombos = {
      ui_->power_input_1_1, ui_->power_input_1_2, ui_->power_input_1_3,
      ui_->power_input_1_4, ui_->power_input_2_1, ui_->power_input_2_4,
      ui_->mk_input_1_1,    ui_->mk_input_1_3,    ui_->mk_input_1_5,
      ui_->mk_input_1_7,    ui_->mk_input_1_9,    ui_->war_input_1};
  for (auto *combo : personCombos) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (uint32_t i = 0; i < personCount; ++i)
      combo->addItem(QStringLiteral("%1:%2").arg(i).arg(personNames[i]), i);
    if (combo != ui_->war_input_1)
      combo->addItem(QStringLiteral("空"), personCount);
  }

  const std::array<QComboBox *, 11> equipmentCombos = {
      ui_->data_input_32,   ui_->data_input_35,   ui_->data_input_38,
      ui_->power_input_2_2, ui_->power_input_2_5, ui_->power_input_3_1,
      ui_->power_input_3_2, ui_->power_input_3_3, ui_->power_input_3_5,
      ui_->power_input_3_6, ui_->power_input_3_7};
  const auto itemTable = profile.address("item_table", 0);
  const auto itemStride = profile.limit("item_stride", 25);
  const auto itemNameSize = profile.limit("item_name_size", 15);
  itemNameCache_.clear();
  // 6.4+ ships item metadata in Data.e5/Star.e5; the live table can be
  // zero-filled until the engine loads resources.  Prefer the read-only
  // split files, then fill any remaining IDs from the runtime table.
  try {
    if (profile.version != EngineVersion::V64 &&
        profile.version != EngineVersion::V65 &&
        profile.version != EngineVersion::V66)
      throw Error("旧版使用运行时物品表");
    const auto root = parentPath(session_.process().path);
    Bytes dataFile, starFile;
    try {
      dataFile = readFile(root + L"\\Data.e5");
    } catch (...) {
    }
    try {
      starFile = readFile(root + L"\\Star.e5");
    } catch (...) {
    }
    constexpr size_t kRowSize = 25;
    constexpr size_t kLowOffset = 33164;
    constexpr size_t kLowCount = 104;
    constexpr size_t kHighOffset = 0;
    constexpr size_t kHighCount = 152;
    constexpr size_t kExtensionOffset = 3800;
    constexpr size_t kExtensionCount = 256;
    const auto readRow = [&](uint32_t id, Bytes &row) {
      size_t offset = 0;
      const Bytes *source = nullptr;
      if (id < kLowCount) {
        source = &dataFile;
        offset = kLowOffset + size_t(id) * kRowSize;
      } else if (id < kLowCount + kHighCount) {
        source = &starFile;
        offset = kHighOffset + size_t(id - kLowCount) * kRowSize;
      } else if (id < kLowCount + kHighCount + kExtensionCount) {
        source = &starFile;
        offset = kExtensionOffset +
                 size_t(id - kLowCount - kHighCount) * kRowSize;
      } else {
        return false;
      }
      if (offset > source->size() || kRowSize > source->size() - offset)
        return false;
      row.assign(source->begin() + offset, source->begin() + offset + kRowSize);
      return true;
    };
    Bytes row;
    for (uint32_t id = 0; id < itemCount; ++id) {
      if (!readRow(id, row))
        continue;
      auto name = legacyTableText(row, 0, std::min<size_t>(itemNameSize, 25));
      if (!name.isEmpty() && name != QStringLiteral("非法字符"))
        itemNameCache_[uint16_t(id)] = name;
    }
  } catch (...) {
  }
  if (itemTable && itemStride && itemNameSize <= itemStride) {
    try {
      const auto itemBytes =
          session->read(itemTable, size_t(itemCount) * itemStride);
      for (uint32_t i = 0; i < itemCount; ++i) {
        if (itemNameCache_.find(uint16_t(i)) != itemNameCache_.end())
          continue;
        auto name = decodeCodePage(
            itemBytes.data() + size_t(i) * itemStride, itemNameSize, 936);
        if (name.isEmpty())
          name = decodeCodePage(
              itemBytes.data() + size_t(i) * itemStride, itemNameSize, 54936);
        if (!name.isEmpty() && name != QStringLiteral("非法字符"))
          itemNameCache_[uint16_t(i)] = name;
      }
    } catch (...) {
      // Keep the individual itemName() reader as a fallback for packed builds.
    }
  }
  for (uint32_t i = 0; i < itemCount; ++i)
    if (itemNameCache_.find(uint16_t(i)) == itemNameCache_.end())
      itemNameCache_[uint16_t(i)] = QStringLiteral("物品%1").arg(i);
  for (auto *combo : equipmentCombos) {
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (uint32_t i = 0; i < equipmentCount; ++i)
      combo->addItem(QStringLiteral("%1:%2").arg(i).arg(itemName(uint16_t(i))),
                     i);
    combo->addItem(QStringLiteral("空"), 0xff);
  }
  {
    const QSignalBlocker blocker(ui_->item_input_1);
    ui_->item_input_1->clear();
    const auto warehouseItems = std::min<uint32_t>(itemCount, 255);
    for (uint32_t i = 0; i < warehouseItems; ++i)
      ui_->item_input_1->addItem(
          QStringLiteral("%1:%2").arg(i).arg(itemName(uint16_t(i))), i);
    ui_->item_input_1->addItem(QStringLiteral("空"), 0xff);
  }

  ui_->data_input_31->clear();
  auto jobs = profile.limit("effect_job_count", 80);
  auto jobNames = profile.address("job_names", 0);
  for (uint32_t i = 0; i < jobs; ++i) {
    QString name;
    if (jobNames) {
      try {
        name = gameText(session,
                        checkedAddress(jobNames, uint64_t(i) * 16, 16), 15);
      } catch (...) {
      }
    }
    if (name.isEmpty())
      name = QStringLiteral("兵种%1").arg(i);
    ui_->data_input_31->addItem(QStringLiteral("%1:%2").arg(i).arg(name), i);
  }
  ui_->data_input_31->addItem(QStringLiteral("空"), 0xff);

  for (auto *combo : {ui_->power_input_1_9, ui_->power_input_1_11}) {
    combo->clear();
    for (uint32_t i = 0; i < jobs; ++i)
      combo->addItem(QStringLiteral("兵种%1").arg(i), i);
    combo->addItem(QStringLiteral("空"), 0xff);
  }

  (void)equipmentCount;
  updateEffectLayout();
  // 只预热人物页：它同时建立物品/兵种/特效名字目录，其余页面改为切页时按需
  // 刷新（showPage → refreshCurrentPage）。此前附加即拉满 8 页，一次要多做
  // 约 1300 次远程读、建 6000 多个表格项，是启动卡顿的主因。
  refreshPeople();
}

void QtMainWindow::refreshCurrentPage() {
  switch (currentPage_) {
  case 0:
    break;
  case 1:
    refreshPeople();
    break;
  case 2:
    refreshBattle(true);
    break;
  case 3:
    refreshWarehouse();
    break;
  case 4:
    refreshEffects();
    break;
  case 5:
    refreshVariables();
    break;
  case 6:
    refreshDiy();
    break;
  case 7:
    refreshFatal();
    break;
  default:
    break;
  }
}

void QtMainWindow::refreshPeople() {
  if (!session_.session())
    return;
  if (ui_->listview_data->count() == 0)
    populateSharedCatalogs();
  if (ui_->listview_data->count() && ui_->listview_data->currentRow() < 0)
    ui_->listview_data->setCurrentRow(0);
  else
    loadPerson();
}

void QtMainWindow::loadPerson() {
  auto session = session_.session();
  auto *selected = ui_->listview_data->currentItem();
  if (!session || !selected)
    return;
  auto id = uint16_t(selected->data(Qt::UserRole).toUInt());
  auto record = PersonService(session, session_.profile()).read(id);
  const auto edits = personEdits(*ui_);
  const std::array<uint64_t, 38> values = {
      record.r, record.s, record.face, record.strength, record.defense,
      record.intelligence, record.agility, record.luck, record.hp, record.mp,
      0, record.level, record.exp, record.baseStats[0], record.baseStats[1],
      record.baseStats[2], record.baseStats[3], record.baseStats[4],
      record.appearance, record.retreat, record.merits[0], record.merits[1],
      record.merits[2], record.merits[3], record.merits[4], record.field28,
      record.field06, record.field44, record.field46, record.kills, 0, 0,
      record.weaponLevel, record.weaponExp, 0, record.armorLevel,
      record.armorExp, 0};
  for (size_t i = 0; i < edits.size(); ++i)
    if (edits[i])
      setText(edits[i], values[i]);
  ui_->data_input_11->setChecked(record.ours == 0);
  selectData(ui_->data_input_31, record.job);
  selectData(ui_->data_input_32, record.weapon);
  selectData(ui_->data_input_35, record.armor);
  selectData(ui_->data_input_38, record.accessory);
  const bool meritAvailable = id < session_.profile().limit("merit_count", 102);
  for (auto *edit : {ui_->data_input_21, ui_->data_input_22,
                     ui_->data_input_23, ui_->data_input_24,
                     ui_->data_input_25})
    edit->setEnabled(meritAvailable);
}

void QtMainWindow::savePerson() {
  auto session = session_.session();
  auto *selected = ui_->listview_data->currentItem();
  if (!session || !selected)
    throw Error("未选择人物");
  auto id = uint16_t(selected->data(Qt::UserRole).toUInt());
  auto record = PersonService(session, session_.profile()).read(id);
  record.r = uint16_t(editUnsigned(ui_->data_input_1, 0xffff, "R形象"));
  record.s = uint16_t(editUnsigned(ui_->data_input_2, 0xffff, "S形象"));
  record.face = uint16_t(editUnsigned(ui_->data_input_3, 0xffff, "头像"));
  record.strength =
      uint16_t(editUnsigned(ui_->data_input_4, 0xffff, "攻击力"));
  record.defense =
      uint16_t(editUnsigned(ui_->data_input_5, 0xffff, "防御力"));
  record.intelligence =
      uint16_t(editUnsigned(ui_->data_input_6, 0xffff, "精神力"));
  record.agility =
      uint16_t(editUnsigned(ui_->data_input_7, 0xffff, "爆发力"));
  record.luck = uint16_t(editUnsigned(ui_->data_input_8, 0xffff, "士气"));
  record.hp = editUnsigned(ui_->data_input_9, UINT32_MAX, "HP上限");
  auto expanded = session_.profile().is66() ||
                  session_.profile().version == EngineVersion::V64 ||
                  session_.profile().version == EngineVersion::V65 ||
                  session_.profile().version == EngineVersion::V63MP;
  record.mp = uint16_t(editUnsigned(ui_->data_input_10,
                                    expanded ? 0xffff : 0xff, "MP上限"));
  record.ours = ui_->data_input_11->isChecked() ? 0 : 0xff;
  record.level = uint8_t(editUnsigned(ui_->data_input_12, 0xff, "等级"));
  record.exp = uint8_t(editUnsigned(ui_->data_input_13, 0xff, "经验"));
  for (size_t i = 0; i < record.baseStats.size(); ++i) {
    auto *edit = std::array<QTextEdit *, 5>{
        ui_->data_input_14, ui_->data_input_15, ui_->data_input_16,
        ui_->data_input_17, ui_->data_input_18}[i];
    record.baseStats[i] = uint8_t(editUnsigned(edit, 0xff, "人物五维"));
  }
  record.appearance =
      uint8_t(editUnsigned(ui_->data_input_19, 0xff, "出场状态"));
  record.retreat =
      uint8_t(editUnsigned(ui_->data_input_20, 0xff, "撤退状态"));
  if (id < session_.profile().limit("merit_count", 102)) {
    const std::array<QTextEdit *, 5> meritEdits = {
        ui_->data_input_21, ui_->data_input_22, ui_->data_input_23,
        ui_->data_input_24, ui_->data_input_25};
    for (size_t i = 0; i < meritEdits.size(); ++i)
      record.merits[i] =
          uint16_t(editUnsigned(meritEdits[i], 0xffff, "人物功勋"));
  }
  record.field28 =
      uint8_t(editUnsigned(ui_->data_input_26, 0xff, "人物字段28"));
  record.field06 =
      uint16_t(editUnsigned(ui_->data_input_27, 0xffff, "人物字段06"));
  record.field44 =
      uint16_t(editUnsigned(ui_->data_input_28, 0xffff, "人物字段44"));
  record.field46 =
      uint16_t(editUnsigned(ui_->data_input_29, 0xffff, "人物字段46"));
  record.kills =
      uint16_t(editUnsigned(ui_->data_input_30, 0xffff, "杀敌数"));
  record.job = comboByte(ui_->data_input_31);
  record.weapon = comboByte(ui_->data_input_32);
  record.weaponLevel =
      uint8_t(editUnsigned(ui_->data_input_33, 0xff, "武器等级"));
  record.weaponExp =
      uint8_t(editUnsigned(ui_->data_input_34, 0xff, "武器经验"));
  record.armor = comboByte(ui_->data_input_35);
  record.armorLevel =
      uint8_t(editUnsigned(ui_->data_input_36, 0xff, "防具等级"));
  record.armorExp =
      uint8_t(editUnsigned(ui_->data_input_37, 0xff, "防具经验"));
  record.accessory = comboByte(ui_->data_input_38);
  if (session_.profile().version == EngineVersion::V64 ||
      session_.profile().version == EngineVersion::V65 ||
      session_.profile().version == EngineVersion::V66) {
    const auto equipmentEnd =
        session_.profile().limit("equipment_count", 150) - 1;
    auto validateEquipment = [&](uint8_t item, uint32_t first, uint32_t last,
                                 const char *label) {
      if (item != 0xff && (item < first || item > last))
        throw Error(std::string(label) + "不能使用消耗品或其他类别物品");
    };
    // 与 Python 一致：武器 0..69、防具 70..108、辅助 109..末尾。
    const uint32_t weaponEnd = std::min<uint32_t>(69, equipmentEnd);
    const uint32_t armorEnd = std::min<uint32_t>(108, equipmentEnd);
    validateEquipment(record.weapon, 0, weaponEnd, "武器");
    validateEquipment(record.armor, 70, armorEnd, "防具");
    validateEquipment(record.accessory, 109, equipmentEnd, "辅助");
  }
  PersonService(session, session_.profile()).write(id, record);
  loadPerson();
  setStatus(QStringLiteral("人物已保存"));
}

void QtMainWindow::recalculatePerson() {
  auto session = session_.session();
  auto *selected = ui_->listview_data->currentItem();
  if (!session || !selected)
    throw Error("未选择人物");
  auto id = selected->data(Qt::UserRole).toUInt();
  auto profile = session_.profile();
  auto base = session->u(profile.address("person_pointer", 0x004CEA00), 4);
  const auto recordAddress =
      checkedAddress(base, uint64_t(id) * 0x48, 0x48);
  const auto before = session->read(recordAddress, 0x48);
  const auto meritBase = profile.address("merit", 0x00508000);
  const auto meritCount = profile.limit("merit_count", 102);
  const auto meritAddress =
      checkedAddress(meritBase, uint64_t(id) * 10, 10);
  const bool hasMerit = id < meritCount;
  const auto meritBefore = hasMerit ? session->read(meritAddress, 10) : Bytes();

  remoteFor(session, profile).recalculate(recordAddress);

  // 与 Python 一致：重算只允许改派生数值，若碰到这些域说明调用约定变了。
  const auto after = session->read(recordAddress, 0x48);
  std::vector<std::string> changed;
  struct InvariantRange {
    size_t begin;
    size_t end;
    const char *label;
  };
  const std::array<InvariantRange, 4> invariantRanges = {{
      {0x1B, 0x21, "HP/MP"},
      {0x21, 0x26, "基础五维"},
      {0x2B, 0x2C, "兵种"},
      {0x2C, 0x2D, "等级"},
  }};
  for (const auto &range : invariantRanges) {
    if (!std::equal(before.begin() + range.begin,
                    before.begin() + range.end,
                    after.begin() + range.begin))
      changed.emplace_back(range.label);
  }
  if (hasMerit && session->read(meritAddress, 10) != meritBefore)
    changed.emplace_back("功勋");
  // 满级（>=60）时经验被写成 0xFF 属正常结算，其余变化一律视为异常。
  if (before[0x2D] != after[0x2D] &&
      !(before[0x2C] >= 60 && after[0x2D] == 0xff))
    changed.emplace_back("经验");
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
  setStatus(QStringLiteral("人物能力已重新计算"));
}

size_t QtMainWindow::selectedBattleSlot() const {
  auto *item = ui_->listview_war->currentItem();
  return item ? size_t(item->data(Qt::UserRole).toULongLong()) : SIZE_MAX;
}

void QtMainWindow::refreshBattle(bool preserveSelection) {
  auto session = session_.session();
  if (!session)
    return;
  const auto selectedSlot = preserveSelection ? selectedBattleSlot() : SIZE_MAX;
  auto profile = session_.profile();
  auto ours = profile.limit("battle_ours", 20);
  auto friends = profile.limit("battle_friends", 40);
  auto enemies = profile.limit("battle_enemies", 190);
  auto faction = ui_->war_kind_1->isChecked() ? 0
                 : ui_->war_kind_2->isChecked() ? 1
                                                : 2;
  auto first = faction == 0 ? 0u : faction == 1 ? ours : ours + friends;
  auto last = faction == 0 ? ours
              : faction == 1 ? ours + friends
                             : ours + friends + enemies;
  QSignalBlocker blocker(ui_->listview_war);
  ui_->listview_war->clear();
  BattleService service(session, profile);
  int restoreRow = -1;
  for (uint32_t slot = first; slot < last; ++slot) {
    auto record = service.read(slot);
    if (record.dataId == 0xffff)
      continue;
    auto *item = new QListWidgetItem(
        QStringLiteral("%1/%2:%3")
            .arg(record.displaySlot)
            .arg(record.dataId)
            .arg(personName(record.dataId)));
    item->setData(Qt::UserRole, slot);
    ui_->listview_war->addItem(item);
    if (slot == selectedSlot)
      restoreRow = ui_->listview_war->count() - 1;
  }
  if (ui_->listview_war->count())
    ui_->listview_war->setCurrentRow(restoreRow >= 0 ? restoreRow : 0);
  blocker.unblock();
  loadBattle();
}

void QtMainWindow::loadBattle() {
  auto session = session_.session();
  auto slot = selectedBattleSlot();
  if (!session || slot == SIZE_MAX)
    return;
  auto profile = session_.profile();
  auto record = BattleService(session, profile).read(slot);
  selectData(ui_->war_input_1, record.dataId);
  setText(ui_->war_input_2, record.hp);
  setText(ui_->war_input_3, record.mp);
  setText(ui_->war_input_4, record.sp);
  const bool expandedHealth =
      profile.is66() ||
      ((profile.version == EngineVersion::V64 ||
        profile.version == EngineVersion::V65) &&
       session->u(0x004240CA, 1, false) == 0xfc);
  if (expandedHealth) {
    for (size_t i = 0; i < conditionDigits_.size(); ++i) {
      conditionDigits_[i]->setVisible(true);
      conditionDigits_[i]->setText(
          QString::number((record.health >> (i * 2)) & 3));
    }
    for (auto *box : {ui_->war_input_5, ui_->war_input_6, ui_->war_input_7,
                      ui_->war_input_8})
      box->setVisible(false);
  } else {
    for (auto *digit : conditionDigits_)
      digit->setVisible(false);
    const std::array<QCheckBox *, 4> conditionBoxes = {
        ui_->war_input_5, ui_->war_input_6, ui_->war_input_7,
        ui_->war_input_8};
    for (size_t i = 0; i < conditionBoxes.size(); ++i) {
      conditionBoxes[i]->setVisible(true);
      conditionBoxes[i]->setChecked((record.health & (2u << i)) != 0);
    }
  }
  setText(ui_->war_input_9, record.x);
  setText(ui_->war_input_10, record.y);
  ui_->war_input_15->setChecked((record.action & 6) != 0);
  ui_->war_input_16->setCurrentIndex(0);
  const std::array<QComboBox *, 6> buffCombos = {
      ui_->war_input_17, ui_->war_input_18, ui_->war_input_19,
      ui_->war_input_20, ui_->war_input_21, ui_->war_input_22};
  for (size_t i = 0; i < buffCombos.size(); ++i)
    buffCombos[i]->setCurrentIndex(record.buffs[i] < 3
                                        ? 0
                                        : record.buffs[i] == 3 ? 1 : 2);
  ui_->war_input_23->setCurrentIndex(0);

  int strategy = 0;
  if (record.ai == 1)
    strategy = 1;
  else if (record.ai == 2)
    strategy = 2;
  else if (record.ai == 0)
    strategy = record.targetSlot == 0xff ? 4 : 3;
  else if (record.ai == 4)
    strategy = record.targetSlot == 0xff ? 6 : 5;
  ui_->war_input_11->setCurrentIndex(strategy);
  setText(ui_->war_input_13, record.strategyX);
  setText(ui_->war_input_14, record.strategyY);
  setText(ui_->war_input_36, record.targetX);
  setText(ui_->war_input_37, record.targetY);
  ui_->war_input_12->clear();
  auto total = profile.limit("battle_ours", 20) +
               profile.limit("battle_friends", 40) +
               profile.limit("battle_enemies", 190);
  BattleService battle(session, profile);
  int targetIndex = -1;
  for (uint32_t i = 0; i < total; ++i) {
    auto candidate = battle.read(i);
    if (candidate.dataId == 0xffff)
      continue;
    ui_->war_input_12->addItem(
        QStringLiteral("%1:%2").arg(candidate.displaySlot).arg(
            personName(candidate.dataId)),
        i);
    if (i == record.targetSlot)
      targetIndex = ui_->war_input_12->count() - 1;
  }
  ui_->war_input_12->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
  refreshBattleStrategy();

  setText(ui_->war_input_38, record.field22);
  setText(ui_->war_input_39, record.field23);
  const std::array<QTextEdit *, 6> expandedEdits = {
      ui_->war_input_40, ui_->war_input_44, ui_->war_input_41,
      ui_->war_input_45, ui_->war_input_42, ui_->war_input_43};
  for (size_t i = 0; i < expandedEdits.size(); ++i) {
    setText(expandedEdits[i], record.expanded[i]);
    expandedEdits[i]->setEnabled(
        profile.limit("battle_stride", profile.is66() ? 0x30 : 0x24) >
        0x24 + i * 2);
  }
  ui_->war_input_46->setChecked(record.ai == 7);
  ui_->war_46->setVisible(ui_->war_kind_2->isChecked());
  ui_->war_input_46->setVisible(ui_->war_kind_2->isChecked());

  auto state = BattleStateService(session, profile).read();
  setText(ui_->war_input_24, state.turnLimit);
  setText(ui_->war_input_25, state.currentTurn);
  setText(ui_->war_input_26, state.nextScenario);
  ui_->war_input_27->setCurrentIndex(state.weather);
  const bool revivable =
      record.dataId != 0xffff && record.hp == 0 && record.visibility == 3;
  ui_->war_life->setEnabled(revivable);
  ui_->war_input_9->setEnabled(revivable);
  ui_->war_input_10->setEnabled(revivable);
}

void QtMainWindow::refreshBattleStrategy() {
  const bool ours = ui_->war_kind_1->isChecked();
  const bool target = ui_->war_input_11->currentIndex() == 3 ||
                      ui_->war_input_11->currentIndex() == 5;
  for (auto *widget : {static_cast<QWidget *>(ui_->war_11),
                       static_cast<QWidget *>(ui_->war_input_11),
                       static_cast<QWidget *>(ui_->war_13),
                       static_cast<QWidget *>(ui_->war_input_13),
                       static_cast<QWidget *>(ui_->war_14),
                       static_cast<QWidget *>(ui_->war_input_14),
                       static_cast<QWidget *>(ui_->war_36),
                       static_cast<QWidget *>(ui_->war_input_36),
                       static_cast<QWidget *>(ui_->war_37),
                       static_cast<QWidget *>(ui_->war_input_37)})
    widget->setVisible(!ours);
  ui_->war_12->setVisible(!ours && target);
  ui_->war_input_12->setVisible(!ours && target);
}

void QtMainWindow::saveBattle() {
  auto session = session_.session();
  auto slot = selectedBattleSlot();
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  auto profile = session_.profile();
  auto service = BattleService(session, profile);
  const bool fullSave =
      (session->u(checkedAddress(profile.address("save_state", 0x004B0770),
                                 6, 1),
                  1) &
       1) != 0;
  auto record = service.read(slot);
  auto oldDirection = record.direction;
  record.dataId = uint16_t(ui_->war_input_1->currentData().toUInt());
  record.hp = editUnsigned(ui_->war_input_2, UINT32_MAX, "当前HP");
  record.mp = editUnsigned(ui_->war_input_3, UINT32_MAX, "当前MP");
  record.sp = uint8_t(editUnsigned(ui_->war_input_4, 0xff, "当前SP"));
  record.x = uint8_t(editUnsigned(ui_->war_input_9, 0xff, "X坐标"));
  record.y = uint8_t(editUnsigned(ui_->war_input_10, 0xff, "Y坐标"));
  record.action = ui_->war_input_15->isChecked()
                      ? uint8_t(record.action | 6)
                      : uint8_t(record.action & 0xf8);
  if (ui_->war_input_16->currentIndex() > 0)
    record.direction = uint8_t(ui_->war_input_16->currentIndex() - 1);
  const bool expandedHealth =
      profile.is66() ||
      ((profile.version == EngineVersion::V64 ||
        profile.version == EngineVersion::V65) &&
       session->u(0x004240CA, 1, false) == 0xfc);
  if (expandedHealth) {
    record.health = 0;
    for (size_t i = 0; i < conditionDigits_.size(); ++i)
      record.health |= uint8_t(
          (unsignedText(conditionDigits_[i]->text(), 3, "健康状态") & 3)
          << (i * 2));
  } else {
    const std::array<QCheckBox *, 4> conditionBoxes = {
        ui_->war_input_5, ui_->war_input_6, ui_->war_input_7,
        ui_->war_input_8};
    record.health = 0;
    for (size_t i = 0; i < conditionBoxes.size(); ++i)
      if (conditionBoxes[i]->isChecked())
        record.health |= uint8_t(2u << i);
  }
  const std::array<QComboBox *, 6> buffCombos = {
      ui_->war_input_17, ui_->war_input_18, ui_->war_input_19,
      ui_->war_input_20, ui_->war_input_21, ui_->war_input_22};
  static const uint8_t buffValues[] = {1, 3, 6};
  for (size_t i = 0; i < buffCombos.size(); ++i)
    record.buffs[i] = buffValues[std::clamp(buffCombos[i]->currentIndex(), 0, 2)];
  if (ui_->war_input_23->currentIndex() > 0) {
    auto all = buffValues[std::clamp(ui_->war_input_23->currentIndex() - 1, 0, 2)];
    record.buffs.fill(all);
  }

  if (!ui_->war_kind_1->isChecked()) {
    record.strategyX =
        uint8_t(editUnsigned(ui_->war_input_13, 0xff, "方针X坐标"));
    record.strategyY =
        uint8_t(editUnsigned(ui_->war_input_14, 0xff, "方针Y坐标"));
    record.targetX =
        uint8_t(editUnsigned(ui_->war_input_36, 0xff, "目标X坐标"));
    record.targetY =
        uint8_t(editUnsigned(ui_->war_input_37, 0xff, "目标Y坐标"));
    static const uint8_t aiValues[] = {3, 1, 2, 0, 0, 4, 4};
    auto strategy = std::clamp(ui_->war_input_11->currentIndex(), 0, 6);
    record.ai = aiValues[strategy];
    if (strategy == 3 || strategy == 5)
      record.targetSlot = uint8_t(ui_->war_input_12->currentData().toUInt());
    else if (strategy == 4 || strategy == 6)
      record.targetSlot = 0xff;
    if (ui_->war_kind_2->isChecked() && ui_->war_input_46->isChecked())
      record.ai = 7;
  }
  record.field22 =
      uint8_t(editUnsigned(ui_->war_input_38, 0xff, "战场字段22"));
  record.field23 =
      uint8_t(editUnsigned(ui_->war_input_39, 0xff, "战场字段23"));
  const std::array<QTextEdit *, 6> expandedEdits = {
      ui_->war_input_40, ui_->war_input_44, ui_->war_input_41,
      ui_->war_input_45, ui_->war_input_42, ui_->war_input_43};
  for (size_t i = 0; i < expandedEdits.size(); ++i)
    if (expandedEdits[i]->isEnabled())
      record.expanded[i] =
          uint8_t(editUnsigned(expandedEdits[i], 0xff, "战场扩展字段"));
  auto state = BattleStateService(session, profile).read();
  state.turnLimit =
      uint8_t(editUnsigned(ui_->war_input_24, 0xff, "回合上限"));
  state.currentTurn =
      uint8_t(editUnsigned(ui_->war_input_25, 0xff, "当前回合"));
  state.nextScenario =
      uint8_t(editUnsigned(ui_->war_input_26, 128, "下一关"));
  state.weather = uint8_t(std::clamp(ui_->war_input_27->currentIndex(), 0, 4));

  if (!fullSave) {
    service.write(slot, record);
    refreshBattle(true);
    setStatus(QStringLiteral("当前保存状态仅写入 HP/MP"));
    return;
  }

  service.writePage(slot, record, state);

  // 坐标刷新在 6.6 的调用约定从未验证（Python 版同样标记为不可用且从不调用），
  // 天气由 writePage 直接写入 state.weather 即可生效。这里只保留方向刷新，
  // 且仅在用户真的改了方向时触发，避免每次保存都打一次远程调用。
  if (oldDirection != record.direction)
    remoteFor(session, profile).changeDirection(record.direction,
                                                 record.dataId);

  applyGroupBuffs();
  refreshBattle(true);
  setStatus(QStringLiteral("战场数据已保存"));
}

void QtMainWindow::connectBattlePerson() {
  auto session = session_.session();
  auto slot = selectedBattleSlot();
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  auto id = BattleService(session, session_.profile()).read(slot).dataId;
  if (id == 0xffff)
    throw Error("当前槽位为空");
  for (int row = 0; row < ui_->listview_data->count(); ++row) {
    if (ui_->listview_data->item(row)->data(Qt::UserRole).toUInt() == id) {
      ui_->listview_data->setCurrentRow(row);
      showPage(1);
      return;
    }
  }
}

void QtMainWindow::reviveBattleUnit() {
  auto session = session_.session();
  auto slot = selectedBattleSlot();
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  auto profile = session_.profile();
  auto record = BattleService(session, profile).read(slot);
  if (record.dataId == 0xffff)
    throw Error("当前槽位为空，不能复活");
  if (record.hp != 0 || record.visibility != 3)
    throw Error("目标不是可复活状态");
  auto direction = ui_->war_input_16->currentIndex() == 0
                       ? record.direction
                       : uint8_t(ui_->war_input_16->currentIndex() - 1);
  auto x = editUnsigned(ui_->war_input_9, 0xff, "复活X坐标");
  auto y = editUnsigned(ui_->war_input_10, 0xff, "复活Y坐标");
  if (direction > 3)
    throw Error("复活方向必须在 0..3 范围内");
  const auto beforeId = record.dataId;
  remoteFor(session, profile).revive(direction, x, y, record.dataId);
  auto after = BattleService(session, profile).read(slot);
  if (after.dataId != beforeId)
    throw Error("复活后 Data ID 发生变化");
  if (after.hp == 0 && after.visibility == 3)
    throw Error("远程调用完成但槽位仍为死亡状态");
  ui_->war_life->setEnabled(false);
  refreshBattle(true);
  setStatus(QStringLiteral("战场单位已复活"));
}

void QtMainWindow::killBattleArea() {
  auto session = session_.session();
  if (!session)
    throw Error("尚未附加游戏");
  auto x1 = editUnsigned(ui_->war_input_47, 0xff, "X1");
  auto y1 = editUnsigned(ui_->war_input_48, 0xff, "Y1");
  auto x2 = editUnsigned(ui_->war_input_49, 0xff, "X2");
  auto y2 = editUnsigned(ui_->war_input_50, 0xff, "Y2");
  if (x1 > x2 || y1 > y2)
    throw Error("区域坐标必须从小到大");
  auto profile = session_.profile();
  auto first = profile.limit("battle_ours", 20) +
               profile.limit("battle_friends", 40);
  auto last = first + profile.limit("battle_enemies", 190);
  BattleService service(session, profile);
  std::vector<std::pair<size_t, BattleRecord>> rows;
  for (uint32_t slot = first; slot < last; ++slot) {
    auto record = service.read(slot);
    // 只清理仍在场上的敌军：visibility < 2 视为已消失/未出场，写 HP 会误伤。
    if (record.dataId != 0xffff && record.visibility >= 2 &&
        record.x >= x1 && record.x <= x2 && record.y >= y1 &&
        record.y <= y2) {
      record.hp = 0;
      rows.emplace_back(size_t(slot), record);
    }
  }
  // 整批一个事务：任一槽写入失败会整体回滚，不留半灭状态。
  service.writeRows(rows);
  refreshBattle(true);
}

void QtMainWindow::addBattleLock() {
  auto session = session_.session();
  auto slot = selectedBattleSlot();
  if (!session || slot == SIZE_MAX)
    throw Error("未选择战场单位");
  if (std::any_of(locked_.begin(), locked_.end(),
                  [slot](const LockedBattleUnit &item) {
                    return item.slot == slot;
                  }))
    return;
  auto record = BattleService(session, session_.profile()).read(slot);
  if (record.dataId == 0xffff)
    throw Error("当前槽位为空，不能锁定");
  locked_.push_back({slot, record.dataId, record.hp, record.mp});
  auto *item = new QListWidgetItem(
      QStringLiteral("槽%1 %2 HP=%3 MP=%4")
          .arg(slot)
          .arg(personName(record.dataId))
          .arg(record.hp)
          .arg(record.mp));
  item->setData(Qt::UserRole, qulonglong(slot));
  ui_->listview_war_2->addItem(item);
  if (!lockTimer_.isActive())
    lockTimer_.start();
}

void QtMainWindow::removeBattleLock() {
  auto *selected = ui_->listview_war_2->currentItem();
  if (!selected)
    return;
  auto slot = size_t(selected->data(Qt::UserRole).toULongLong());
  locked_.erase(std::remove_if(locked_.begin(), locked_.end(),
                               [slot](const LockedBattleUnit &item) {
                                 return item.slot == slot;
                               }),
                locked_.end());
  delete ui_->listview_war_2->takeItem(ui_->listview_war_2->currentRow());
  if (locked_.empty())
    lockTimer_.stop();
}

void QtMainWindow::runBattleLocks() {
  auto session = session_.session();
  if (!session || !session->alive()) {
    stopRecurringOperations();
    return;
  }
  auto profile = session_.profile();
  auto base = profile.address("battle_units", 0x004A7B20);
  auto stride = profile.limit("battle_stride", profile.is66() ? 0x30 : 0x24);
  for (auto it = locked_.begin(); it != locked_.end();) {
    auto record = BattleService(session, profile).read(it->slot);
    if (record.dataId != it->dataId) {
      it = locked_.erase(it);
      continue;
    }
    auto address = checkedAddress(base, uint64_t(it->slot) * stride, stride);
    session->writeU(address + 0x10, it->hp, 4, false);
    session->writeU(address + 0x14, it->mp, 4, false);
    ++it;
  }
  for (int row = ui_->listview_war_2->count() - 1; row >= 0; --row) {
    auto slot = size_t(ui_->listview_war_2->item(row)
                           ->data(Qt::UserRole)
                           .toULongLong());
    if (std::none_of(locked_.begin(), locked_.end(),
                     [slot](const LockedBattleUnit &item) {
                       return item.slot == slot;
                     }))
      delete ui_->listview_war_2->takeItem(row);
  }
  if (locked_.empty())
    lockTimer_.stop();
}

void QtMainWindow::runAutoRevive() {
  auto session = session_.session();
  if (!session || !session->alive()) {
    autoLifeTimer_.stop();
    ui_->checkBox_9->setChecked(false);
    return;
  }
  auto profile = session_.profile();
  BattleService battle(session, profile);
  const auto now = std::chrono::steady_clock::now();
  for (auto it = autoReviveAttempts_.begin();
       it != autoReviveAttempts_.end();) {
    if (now - it->second > std::chrono::seconds(2))
      it = autoReviveAttempts_.erase(it);
    else
      ++it;
  }
  for (uint32_t slot = 0; slot < profile.limit("battle_ours", 20); ++slot) {
    auto record = battle.read(slot);
    for (auto it = autoReviveAttempts_.begin();
         it != autoReviveAttempts_.end();) {
      if (it->first.first == slot && it->first.second != record.dataId)
        it = autoReviveAttempts_.erase(it);
      else
        ++it;
    }
    const auto key = std::make_pair(size_t(slot), record.dataId);
    if (record.dataId == 0xffff || record.hp != 0 || record.visibility != 3) {
      autoReviveAttempts_.erase(key);
      continue;
    }
    if (autoReviveAttempts_.find(key) != autoReviveAttempts_.end())
      continue;
    autoReviveAttempts_[key] = now;
    remoteFor(session, profile)
        .revive(record.direction, record.x, record.y, record.dataId);
    auto after = battle.read(slot);
    if (after.dataId != record.dataId)
      throw Error("自动复活后 Data ID 发生变化");
    if (after.hp == 0 && after.visibility == 3)
      throw Error("自动复活调用完成但槽位仍为死亡状态");
  }
}

void QtMainWindow::applyGroupBuffs() {
  auto session = session_.session();
  if (!session)
    return;
  auto profile = session_.profile();
  auto faction = std::clamp(ui_->war_input_28->currentIndex(), 0, 2);
  auto ours = profile.limit("battle_ours", 20);
  auto friends = profile.limit("battle_friends", 40);
  auto enemies = profile.limit("battle_enemies", 190);
  auto first = faction == 0 ? 0u : faction == 1 ? ours : ours + friends;
  auto last = faction == 0 ? ours
              : faction == 1 ? ours + friends
                             : ours + friends + enemies;
  const std::array<QComboBox *, 6> combos = {
      ui_->war_input_29, ui_->war_input_30, ui_->war_input_31,
      ui_->war_input_32, ui_->war_input_33, ui_->war_input_34};
  auto service = BattleStateService(session, profile);
  auto all = ui_->war_input_35->currentIndex();
  for (size_t i = 0; i < combos.size(); ++i) {
    auto choice = all > 0 ? all : combos[i]->currentIndex();
    if (choice > 0)
      service.applyGroupBuff(first, last, i, 2 * choice - 1);
    combos[i]->setCurrentIndex(0);
  }
  ui_->war_input_35->setCurrentIndex(0);
}

void QtMainWindow::refreshWarehouse() {
  auto session = session_.session();
  if (!session)
    return;
  auto profile = session_.profile();
  WarehouseService service(session, profile);
  const auto itemCount = profile.limit("item_count", 255);
  auto oldWarehouse = ui_->listview_item->currentRow();
  auto oldConsumable = ui_->listview_item_2->currentRow();
  {
    const QSignalBlocker blocker(ui_->listview_item);
    ui_->listview_item->setUpdatesEnabled(false);
    ui_->listview_item->clear();
    const auto records = service.readRecords(200);
    for (size_t slot = 0; slot < records.size(); ++slot) {
      const auto &record = records[slot];
      auto text = warehouseRowText(record.item, itemCount,
                                   itemName(record.item));
      auto *item = new QListWidgetItem(text);
      item->setData(Qt::UserRole, qulonglong(slot));
      ui_->listview_item->addItem(item);
    }
    ui_->listview_item->setUpdatesEnabled(true);
    ui_->listview_item->setCurrentRow(
        std::clamp(oldWarehouse, 0, ui_->listview_item->count() - 1));
  }
  {
    const QSignalBlocker blocker(ui_->listview_item_2);
    ui_->listview_item_2->clear();
    for (size_t index = 0; index < service.consumableCount(); ++index) {
      auto id = service.consumableItemId(index);
      auto *item = new QListWidgetItem(
          QStringLiteral("%1:%2").arg(id).arg(itemName(id)));
      item->setData(Qt::UserRole, qulonglong(index));
      ui_->listview_item_2->addItem(item);
    }
    if (ui_->listview_item_2->count())
      ui_->listview_item_2->setCurrentRow(
          std::clamp(oldConsumable, 0, ui_->listview_item_2->count() - 1));
  }
  loadWarehouseRecord();
  loadConsumable();
}

void QtMainWindow::loadWarehouseRecord() {
  auto session = session_.session();
  auto *selected = ui_->listview_item->currentItem();
  if (!session || !selected)
    return;
  auto slot = size_t(selected->data(Qt::UserRole).toULongLong());
  auto profile = session_.profile();
  auto record = WarehouseService(session, profile).readRecord(slot);
  selectData(ui_->item_input_1, record.item);
  setText(ui_->item_input_2, record.level);
  setText(ui_->item_input_3, record.experience);
  setText(ui_->item_input_5,
          session->u(profile.address("money", 0x004B077C), 4));
  setText(ui_->item_input_6,
          session->u(profile.address("alignment", 0x004B0782), 1));
  setText(ui_->item_input_7,
          session->u(profile.address("merit_pool", 0x00505F40), 4));
}

void QtMainWindow::loadConsumable() {
  auto session = session_.session();
  auto *selected = ui_->listview_item_2->currentItem();
  if (!session || !selected)
    return;
  auto index = size_t(selected->data(Qt::UserRole).toULongLong());
  setText(ui_->item_input_4,
          WarehouseService(session, session_.profile()).readConsumable(index));
}

void QtMainWindow::saveWarehouse() {
  auto session = session_.session();
  auto *selected = ui_->listview_item->currentItem();
  if (!session || !selected)
    throw Error("未选择仓库槽位");
  auto profile = session_.profile();
  auto slot = size_t(selected->data(Qt::UserRole).toULongLong());
  WarehouseRecord record{};
  record.item = comboByte(ui_->item_input_1);
  record.level =
      uint8_t(editUnsigned(ui_->item_input_2, 0xff, "装备等级"));
  record.experience =
      uint8_t(editUnsigned(ui_->item_input_3, 0xff, "装备经验"));
  WarehouseService service(session, profile);
  WarehousePageEdit edit;
  edit.index = slot;
  edit.record = record;
  edit.writeRecord = true;
  if (auto *consumable = ui_->listview_item_2->currentItem()) {
    edit.writeConsumable = true;
    edit.consumableIndex =
        size_t(consumable->data(Qt::UserRole).toULongLong());
    edit.consumableValue =
        uint8_t(editUnsigned(ui_->item_input_4, 0xff, "道具数量"));
  }
  edit.writeGlobals = true;
  edit.money = editUnsigned(ui_->item_input_5, UINT32_MAX, "金钱");
  edit.alignment = uint8_t(editUnsigned(ui_->item_input_6, 0xff, "忠奸度"));
  edit.meritPool = editUnsigned(ui_->item_input_7, UINT32_MAX, "功勋池");
  service.writePage(edit);
  refreshWarehouse();
  setStatus(QStringLiteral("仓库已保存"));
}

void QtMainWindow::warehouseBulk(int action) {
  auto session = session_.session();
  if (!session)
    throw Error("尚未附加游戏");
  WarehouseService service(session, session_.profile());
  if (action == 1) {
    auto level = uint8_t(editUnsigned(ui_->item_input_2, 9, "装备等级"));
    auto experience =
        uint8_t(editUnsigned(ui_->item_input_3, 0xff, "装备经验"));
    auto count = service.addAllTreasures(level, experience);
    setStatus(QStringLiteral("已写入 %1 件宝物").arg(count));
  } else if (action == 2) {
    service.setAllConsumables(0xff);
    setStatus(QStringLiteral("所有道具数量已设为 255"));
  } else {
    service.clearWarehouse();
    service.clearConsumables();
    setStatus(QStringLiteral("仓库和道具已清空"));
  }
  refreshWarehouse();
}

void QtMainWindow::updateEffectLayout() {
  auto session = session_.session();
  const bool extended =
      session && session_.profile().limit("effect_stride", 8) >= 16;
  const std::array<QWidget *, 12> extendedOnly = {
      ui_->power_label_1_2, ui_->power_label_1_3, ui_->power_label_1_4,
      ui_->power_label_1_5, ui_->power_label_1_6, ui_->power_input_1_6,
      ui_->power_input_1_7, ui_->power_input_1_8, ui_->power_input_1_9,
      ui_->power_input_1_10, ui_->power_input_1_11, ui_->power_input_1_12};
  for (auto *widget : extendedOnly)
    widget->setVisible(extended);
  if (extended) {
    ui_->power_1->setGeometry(185, 15, 136, 20);
    ui_->power_1->setText(QStringLiteral("天赋·角色1 / 值"));
    const std::array<QWidget *, 6> wide = {
        ui_->power_input_1_1, ui_->power_input_1_2, ui_->power_input_1_3,
        ui_->power_input_1_4, ui_->power_input_1_9, ui_->power_input_1_11};
    const std::array<QPoint, 6> wideAt = {
        QPoint(185, 40), QPoint(355, 40), QPoint(525, 40),
        QPoint(185, 80), QPoint(355, 80), QPoint(525, 80)};
    for (size_t i = 0; i < wide.size(); ++i)
      wide[i]->setGeometry(wideAt[i].x(), wideAt[i].y(), 96, 25);
    const std::array<QWidget *, 6> values = {
        ui_->power_input_1_5, ui_->power_input_1_6, ui_->power_input_1_7,
        ui_->power_input_1_8, ui_->power_input_1_10, ui_->power_input_1_12};
    const std::array<QPoint, 6> valueAt = {
        QPoint(285, 40), QPoint(455, 40), QPoint(625, 40),
        QPoint(285, 80), QPoint(455, 80), QPoint(625, 80)};
    for (size_t i = 0; i < values.size(); ++i)
      values[i]->setGeometry(valueAt[i].x(), valueAt[i].y(), 36, 25);
  } else {
    ui_->power_1->setGeometry(185, 15, 31, 20);
    ui_->power_1->setText(QStringLiteral("天赋"));
    ui_->power_input_1_1->setGeometry(185, 40, 136, 25);
    ui_->power_input_1_2->setGeometry(355, 40, 136, 25);
    ui_->power_input_1_3->setGeometry(525, 40, 136, 25);
    ui_->power_input_1_4->setGeometry(185, 80, 136, 25);
    ui_->power_input_1_5->setGeometry(355, 80, 136, 25);
  }
}

void QtMainWindow::refreshEffects() {
  auto session = session_.session();
  if (!session)
    return;
  auto profile = session_.profile();
  updateEffectLayout();
  auto selected = ui_->listview_item_3->currentRow();
  QSignalBlocker blocker(ui_->listview_item_3);
  ui_->listview_item_3->clear();
  auto count = profile.limit("effect_name_count",
                             profile.limit("effect_count", 255));
  auto names = profile.address("effect_names", 0);
  auto nameSize = profile.limit("effect_name_size", 15);
  auto nameStride = profile.limit("effect_name_stride", 16);
  if (!names) {
    switch (profile.version) {
    case EngineVersion::V64:
      names = 0x004FF560;
      count = std::min<uint32_t>(count, 180);
      break;
    case EngineVersion::V63MP:
    case EngineVersion::V63:
      names = 0x004FF7A0;
      count = std::min<uint32_t>(count, 144);
      break;
    case EngineVersion::V62:
      try {
        names = session->u(0x004F0000, 1, false) != 0 ? 0x004F0000
                                                       : 0x004FF960;
      } catch (...) {
        names = 0x004FF960;
      }
      count = std::min<uint32_t>(count, names == 0x004F0000 ? 126 : 91);
      break;
    case EngineVersion::V61:
      names = 0x004FFA80;
      count = std::min<uint32_t>(count, 88);
      break;
    default:
      break;
    }
  }
  for (uint32_t id = 0; id < count; ++id) {
    QString name;
    if (names) {
      try {
        name = gameText(session,
                        checkedAddress(names, uint64_t(id) * nameStride,
                                       nameSize),
                        nameSize);
      } catch (...) {
      }
    }
    if (name.isEmpty())
      name = QStringLiteral("特效%1").arg(id);
    auto *item = new QListWidgetItem(QStringLiteral("%1:%2").arg(id).arg(name));
    item->setData(Qt::UserRole, id);
    ui_->listview_item_3->addItem(item);
  }
  if (ui_->listview_item_3->count())
    ui_->listview_item_3->setCurrentRow(
        std::clamp(selected, 0, ui_->listview_item_3->count() - 1));
  blocker.unblock();
  loadEffect();
}

void QtMainWindow::loadEffect() {
  auto session = session_.session();
  auto *selected = ui_->listview_item_3->currentItem();
  if (!session || !selected)
    return;
  auto id = size_t(selected->data(Qt::UserRole).toULongLong());
  auto profile = session_.profile();
  auto record = EffectService(session, profile).read(id);
  if (profile.limit("effect_stride", 8) >= 16) {
    const std::array<QComboBox *, 4> characters = {
        ui_->power_input_1_1, ui_->power_input_1_2,
        ui_->power_input_1_3, ui_->power_input_1_4};
    const std::array<QTextEdit *, 4> values = {
        ui_->power_input_1_5, ui_->power_input_1_6,
        ui_->power_input_1_7, ui_->power_input_1_8};
    for (size_t i = 0; i < characters.size(); ++i) {
      selectData(characters[i], record.characters[i]);
      setText(values[i], record.characterValues[i]);
    }
    selectData(ui_->power_input_1_9, record.jobs[0]);
    selectData(ui_->power_input_1_11, record.jobs[1]);
    setText(ui_->power_input_1_10, record.jobValues[0]);
    setText(ui_->power_input_1_12, record.jobValues[1]);
  } else {
    selectData(ui_->power_input_1_1, record.characters[0]);
    selectData(ui_->power_input_1_2, record.characters[1]);
    selectData(ui_->power_input_1_3, record.characters[2]);
    selectData(ui_->power_input_1_4, record.jobs[0]);
    setText(ui_->power_input_1_5, record.jobValues[0]);
  }
  auto exclusive = ExclusiveEffectService(session, profile).read(id);
  const std::array<QComboBox *, 2> persons = {
      ui_->power_input_2_1, ui_->power_input_2_4};
  const std::array<QComboBox *, 2> personItems = {
      ui_->power_input_2_2, ui_->power_input_2_5};
  const std::array<QTextEdit *, 2> personValues = {
      ui_->power_input_2_3, ui_->power_input_2_6};
  for (size_t i = 0; i < 2; ++i) {
    selectData(persons[i], exclusive.personValues[i]
                               ? exclusive.persons[i]
                               : profile.limit("person_count", 1024));
    selectData(personItems[i], exclusive.personValues[i]
                                   ? exclusive.personItems[i]
                                   : 0xff);
    setText(personValues[i], exclusive.personValues[i]);
  }
  const std::array<std::array<QComboBox *, 3>, 2> setItems = {{
      {ui_->power_input_3_1, ui_->power_input_3_2, ui_->power_input_3_3},
      {ui_->power_input_3_5, ui_->power_input_3_6, ui_->power_input_3_7}}};
  const std::array<QTextEdit *, 2> setValues = {
      ui_->power_input_3_4, ui_->power_input_3_8};
  for (size_t i = 0; i < 2; ++i) {
    for (size_t j = 0; j < 3; ++j)
      selectData(setItems[i][j],
                 exclusive.setValues[i] ? exclusive.setItems[i][j] : 0xff);
    setText(setValues[i], exclusive.setValues[i]);
  }
}

void QtMainWindow::saveEffect() {
  auto session = session_.session();
  auto *selected = ui_->listview_item_3->currentItem();
  if (!session || !selected)
    throw Error("未选择天赋记录");
  auto id = size_t(selected->data(Qt::UserRole).toULongLong());
  auto profile = session_.profile();
  EffectRecord record{};
  if (profile.limit("effect_stride", 8) >= 16) {
    const std::array<QComboBox *, 4> characters = {
        ui_->power_input_1_1, ui_->power_input_1_2,
        ui_->power_input_1_3, ui_->power_input_1_4};
    const std::array<QTextEdit *, 4> values = {
        ui_->power_input_1_5, ui_->power_input_1_6,
        ui_->power_input_1_7, ui_->power_input_1_8};
    for (size_t i = 0; i < characters.size(); ++i) {
      record.characters[i] = uint16_t(characters[i]->currentData().toUInt());
      record.characterValues[i] =
          uint8_t(editUnsigned(values[i], 0xff, "天赋值"));
    }
    record.jobs[0] = comboByte(ui_->power_input_1_9);
    record.jobs[1] = comboByte(ui_->power_input_1_11);
    record.jobValues[0] =
        uint8_t(editUnsigned(ui_->power_input_1_10, 0xff, "兵种天赋值"));
    record.jobValues[1] =
        uint8_t(editUnsigned(ui_->power_input_1_12, 0xff, "兵种天赋值"));
  } else {
    record.characters[0] = uint16_t(ui_->power_input_1_1->currentData().toUInt());
    record.characters[1] = uint16_t(ui_->power_input_1_2->currentData().toUInt());
    record.characters[2] = uint16_t(ui_->power_input_1_3->currentData().toUInt());
    record.jobs[0] = comboByte(ui_->power_input_1_4);
    record.jobValues[0] =
        uint8_t(editUnsigned(ui_->power_input_1_5, 0xff, "天赋值"));
  }
  EffectService(session, profile).write(id, record);

  ExclusiveEffectRecord exclusive{};
  const std::array<QComboBox *, 2> persons = {
      ui_->power_input_2_1, ui_->power_input_2_4};
  const std::array<QComboBox *, 2> personItems = {
      ui_->power_input_2_2, ui_->power_input_2_5};
  const std::array<QTextEdit *, 2> personValues = {
      ui_->power_input_2_3, ui_->power_input_2_6};
  for (size_t i = 0; i < 2; ++i) {
    exclusive.personValues[i] =
        uint8_t(editUnsigned(personValues[i], 0xff, "专属特效值"));
    if (exclusive.personValues[i]) {
      exclusive.persons[i] = uint16_t(persons[i]->currentData().toUInt());
      exclusive.personItems[i] = comboByte(personItems[i]);
    }
  }
  const std::array<std::array<QComboBox *, 3>, 2> setItems = {{
      {ui_->power_input_3_1, ui_->power_input_3_2, ui_->power_input_3_3},
      {ui_->power_input_3_5, ui_->power_input_3_6, ui_->power_input_3_7}}};
  const std::array<QTextEdit *, 2> setValues = {
      ui_->power_input_3_4, ui_->power_input_3_8};
  for (size_t i = 0; i < 2; ++i) {
    exclusive.setValues[i] =
        uint8_t(editUnsigned(setValues[i], 0xff, "套装特效值"));
    if (exclusive.setValues[i])
      for (size_t j = 0; j < 3; ++j)
        exclusive.setItems[i][j] = comboByte(setItems[i][j]);
  }
  ExclusiveEffectService(session, profile).write(id, exclusive);
  loadEffect();
  setStatus(QStringLiteral("天赋、专属和套装已保存"));
}

void QtMainWindow::refreshVariables() {
  auto session = session_.session();
  if (!session)
    return;
  const std::array<QTextEdit *, 3> startEdits = {
      ui_->var_input_1, ui_->var_input_2, ui_->var_input_3};
  const std::array<QTableWidget *, 3> tables = {
      ui_->listview_var_1, ui_->listview_var_2, ui_->listview_var_3};
  VariableService service(session, session_.profile());
  for (size_t kind = 0; kind < tables.size(); ++kind) {
    const auto maximum = service.browseCount(VariableKind(kind));
    auto start = size_t(editUnsigned(startEdits[kind],
                                     maximum ? uint32_t(maximum - 1) : 0,
                                     "变量起始索引"));
    auto cells = service.readPage(VariableKind(kind), start,
                                  std::min<size_t>(100, maximum - start));
    QSignalBlocker blocker(tables[kind]);
    tables[kind]->setSortingEnabled(false);
    tables[kind]->setRowCount(int(cells.size()));
    for (size_t row = 0; row < cells.size(); ++row) {
      const auto &cell = cells[row];
      auto *indexItem = new QTableWidgetItem(QString::number(cell.index));
      indexItem->setFlags(indexItem->flags() & ~Qt::ItemIsEditable);
      QString value;
      if (kind == 0)
        value = cell.value ? QStringLiteral("True") : QStringLiteral("False");
      else if (kind == 1)
        value = QString::number(int32_t(cell.value));
      else
        value = QString::number(cell.value);
      auto *valueItem = new QTableWidgetItem(value);
      if (kind == 0)
        valueItem->setForeground(cell.value ? QColor(255, 0, 0)
                                            : QColor(0, 0, 255));
      auto *addressItem = new QTableWidgetItem(hexAddress(cell.address));
      addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
      tables[kind]->setItem(int(row), 0, indexItem);
      tables[kind]->setItem(int(row), 1, valueItem);
      tables[kind]->setItem(int(row), 2, addressItem);
    }
    tables[kind]->setSortingEnabled(false);
    if (!cells.empty())
      tables[kind]->setCurrentCell(0, 1);
  }
}

void QtMainWindow::saveVariable(int kind) {
  auto session = session_.session();
  if (!session || kind < 0 || kind >= 3)
    throw Error("尚未附加游戏");
  const std::array<QTableWidget *, 3> tables = {
      ui_->listview_var_1, ui_->listview_var_2, ui_->listview_var_3};
  auto *table = tables[size_t(kind)];
  auto row = table->currentRow();
  if (row < 0 || !table->item(row, 0) || !table->item(row, 1))
    throw Error("未选择变量");
  VariableService service(session, session_.profile());
  const auto maximum = service.recordCount(VariableKind(kind));
  auto index = size_t(unsignedText(table->item(row, 0)->text(),
                                   maximum ? uint32_t(maximum - 1) : 0,
                                   "变量索引"));
  auto value = table->item(row, 1)->text().trimmed();
  if (kind == 0) {
    auto lowered = value.toLower();
    service.writeBool(index, lowered == QStringLiteral("true") ||
                                 lowered == QStringLiteral("t") ||
                                 lowered == QStringLiteral("1"));
  } else if (kind == 1) {
    service.writeInt(index, signedText(value, "整数变量"));
  } else {
    service.writePointer(index,
                         unsignedText(value, UINT32_MAX, "指针变量"));
  }
  refreshVariables();
}

void QtMainWindow::setupBooleanActions() {
  const auto install = [this](QWidget *target) {
    auto *saveRow = new QAction(QStringLiteral("保存选中行"), target);
    connect(saveRow, &QAction::triggered, this, [this] {
      guarded(QStringLiteral("保存变量"), [this] { saveVariable(0); });
    });
    auto *savePage = new QAction(QStringLiteral("保存本页全部修改"), target);
    connect(savePage, &QAction::triggered, this, [this] {
      guarded(QStringLiteral("保存布尔页"), [this] { saveBooleanPage(); });
    });
    auto *exportAction = new QAction(QStringLiteral("全量导出…"), target);
    connect(exportAction, &QAction::triggered, this, [this] {
      guarded(QStringLiteral("布尔全量导出"),
              [this] { exportBooleanArchive(); });
    });
    auto *importAction =
        new QAction(QStringLiteral("全量导入并保存…"), target);
    connect(importAction, &QAction::triggered, this, [this] {
      guarded(QStringLiteral("布尔全量导入"),
              [this] { importBooleanArchive(); });
    });
    target->addAction(saveRow);
    target->addAction(savePage);
    target->addAction(exportAction);
    target->addAction(importAction);
    target->setContextMenuPolicy(Qt::ActionsContextMenu);
  };
  install(ui_->listview_var_1);
  install(variableSave_[0]);
  variableSave_[0]->setToolTip(QStringLiteral(
      "点击保存选中行；右键可保存本页全部修改、全量导出或全量导入。"));
}

void QtMainWindow::saveBooleanPage() {
  auto session = session_.session();
  if (!session)
    throw Error("尚未附加游戏");
  auto *table = ui_->listview_var_1;
  VariableService service(session, session_.profile());
  const auto maximum = service.browseCount(VariableKind::Boolean);
  std::vector<std::pair<size_t, bool>> values;
  values.reserve(size_t(table->rowCount()));
  for (int row = 0; row < table->rowCount(); ++row) {
    auto *indexItem = table->item(row, 0);
    auto *valueItem = table->item(row, 1);
    if (!indexItem || !valueItem)
      continue;
    auto index = size_t(unsignedText(indexItem->text(),
                                     maximum ? uint32_t(maximum - 1) : 0,
                                     "变量索引"));
    auto value = valueItem->text().trimmed().toLower();
    values.emplace_back(index, value == QStringLiteral("true") ||
                                   value == QStringLiteral("t") ||
                                   value == QStringLiteral("1"));
  }
  service.writeBools(values);
  refreshVariables();
  setStatus(
      QStringLiteral("已保存 %1 项布尔变量").arg(qulonglong(values.size())));
}

void QtMainWindow::applyCapabilities() {
  const std::array<QCheckBox *, 7> patchControls = {
      ui_->checkBox_1, ui_->checkBox_2, ui_->checkBox_3, ui_->checkBox_4,
      ui_->checkBox_5, ui_->checkBox_6, ui_->checkBox_7};
  const std::array<QWidget *, 9> gatedWidgets = {
      ui_->data_save,   ui_->war_save,    ui_->item_save,
      ui_->item_save_2, ui_->item_save_4, variableSave_[0],
      variableSave_[1], variableSave_[2], ui_->diy_input_2};
  const auto enableAll = [&] {
    for (auto *box : patchControls) {
      box->setEnabled(true);
      box->setToolTip(QString());
    }
    for (auto *widget : gatedWidgets) {
      widget->setEnabled(true);
      widget->setToolTip(QString());
    }
  };
  enableAll();
}

void QtMainWindow::exportBooleanArchive() {
  auto session = session_.session();
  if (!session)
    throw Error("尚未附加游戏");
  VariableService service(session, session_.profile());
  const auto count = service.writableCount(VariableKind::Boolean);
  if (!count)
    throw Error("当前画像没有可写的布尔变量域");
  std::vector<bool> values;
  values.reserve(count);
  for (size_t index = 0; index < count; ++index)
    values.push_back(service.readBool(index));
  const auto archive = encodeBooleanArchive(values, count);
  const auto path = QFileDialog::getSaveFileName(
      this, QStringLiteral("导出布尔变量"),
      QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath(QStringLiteral("布尔变量.cczbool")),
      QStringLiteral("布尔归档 (*.cczbool);;所有文件 (*)"));
  if (path.isEmpty())
    return;
  writeFile(path.toStdWString(), archive);
  setStatus(QStringLiteral(
                "已全量导出 %1 项布尔变量，共 %2 字节；页面未保存修改不包含在内")
                .arg(qulonglong(count))
                .arg(qulonglong(archive.size())));
}

void QtMainWindow::importBooleanArchive() {
  auto session = session_.session();
  if (!session)
    throw Error("尚未附加游戏");
  VariableService service(session, session_.profile());
  const auto count = service.writableCount(VariableKind::Boolean);
  if (!count)
    throw Error("当前画像没有可写的布尔变量域");
  const auto path = QFileDialog::getOpenFileName(
      this, QStringLiteral("导入布尔变量"),
      QDir(QCoreApplication::applicationDirPath())
          .absoluteFilePath(QStringLiteral("布尔变量.cczbool")),
      QStringLiteral("布尔归档 (*.cczbool);;所有文件 (*)"));
  if (path.isEmpty())
    return;
  const auto values =
      decodeBooleanArchive(readFile(path.toStdWString()), count);
  std::vector<std::pair<size_t, bool>> changed;
  for (size_t index = 0; index < count && index < values.size(); ++index)
    if (service.readBool(index) != values[index])
      changed.emplace_back(index, values[index]);
  service.writeBools(changed);
  refreshVariables();
  setStatus(QStringLiteral("已全量导入 %1 项布尔变量，实际修改 %2 项")
                .arg(qulonglong(count))
                .arg(qulonglong(changed.size())));
}

void QtMainWindow::loadDiy() {
  if (diyPath_.empty()) {
    ui_->diy_input_4->clear();
    ui_->listview_diy->setRowCount(0);
    return;
  }
  auto sections = diy_.sectionNames();
  QSignalBlocker blocker(ui_->diy_input_4);
  ui_->diy_input_4->clear();
  for (const auto &section : sections)
    ui_->diy_input_4->addItem(fromUtf8(section));
  if (!sections.empty()) {
    diySection_ = std::min(diySection_, sections.size() - 1);
    ui_->diy_input_4->setCurrentIndex(int(diySection_));
  }
  blocker.unblock();
  refreshDiy();
}

void QtMainWindow::refreshDiy() {
  auto session = session_.session();
  if (!session || diyPath_.empty() || diy_.sectionCount() == 0) {
    ui_->listview_diy->setRowCount(0);
    return;
  }
  diyVisible_ = diy_.expand(diySection_, diyOffset_, 1000);
  QSignalBlocker blocker(ui_->listview_diy);
  ui_->listview_diy->setSortingEnabled(false);
  ui_->listview_diy->setRowCount(int(diyVisible_.size()));
  auto profile = session_.profile();
  for (size_t row = 0; row < diyVisible_.size(); ++row) {
    auto &item = diyVisible_[row];
    QString explanation;
    try {
      item.value = uint32_t(session->u(item.address, item.width, false));
      if (item.kind == "战场编号") {
        auto total = profile.limit("battle_ours", 20) +
                     profile.limit("battle_friends", 40) +
                     profile.limit("battle_enemies", 190);
        if (item.value < total) {
          auto unit = BattleService(session, profile).read(item.value);
          explanation = unit.dataId == 0xffff
                            ? QStringLiteral("空")
                            : personName(unit.dataId);
        } else {
          explanation = QStringLiteral("无效编号");
        }
      } else if (item.kind == "Data编号") {
        explanation = item.value < profile.limit("person_count", 1024)
                          ? personName(uint16_t(item.value))
                          : QStringLiteral("无效编号");
      } else if (item.kind == "宝物") {
        explanation = item.value < profile.limit("item_count", 255)
                          ? itemName(uint16_t(item.value))
                          : QStringLiteral("无效编号");
      }
    } catch (const std::exception &error) {
      explanation = QStringLiteral("不可读：") + fromUtf8(error.what());
    }
    const std::array<QString, 6> columns = {
        fromUtf8(item.label), QString::number(item.value), explanation,
        hexAddress(item.address), QString::number(item.width),
        fromUtf8(item.explanation)};
    for (size_t column = 0; column < columns.size(); ++column) {
      auto *cell = new QTableWidgetItem(columns[column]);
      if (column != 1)
        cell->setFlags(cell->flags() & ~Qt::ItemIsEditable);
      ui_->listview_diy->setItem(int(row), int(column), cell);
    }
  }
  if (!diyVisible_.empty())
    ui_->listview_diy->setCurrentCell(0, 1);
}

void QtMainWindow::advanceDiyWindow(int scrollValue) {
  if (diyPath_.empty() || diyVisible_.size() < 1000)
    return;
  const auto maximum = ui_->listview_diy->verticalScrollBar()->maximum();
  if (scrollValue >= maximum) {
    const auto nextOffset = diyOffset_ + diyVisible_.size();
    if (!diy_.expand(diySection_, nextOffset, 1).empty()) {
      diyOffset_ = nextOffset;
      refreshDiy();
    }
  } else if (scrollValue == 0 && diyOffset_ >= 1000) {
    diyOffset_ -= 1000;
    refreshDiy();
  }
}

void QtMainWindow::saveDiyValue() {
  auto session = session_.session();
  auto row = ui_->listview_diy->currentRow();
  if (!session || row < 0 || size_t(row) >= diyVisible_.size() ||
      !ui_->listview_diy->item(row, 1))
    throw Error("未选择自定义字段");
  auto &item = diyVisible_[size_t(row)];
  uint64_t maximum = item.width == 4 ? UINT32_MAX
                                      : (uint64_t(1) << (item.width * 8)) - 1;
  auto value = unsignedText(ui_->listview_diy->item(row, 1)->text(),
                            uint32_t(maximum), "自定义值");
  DiyService(session).write(item.address, value, item.width);
  item.value = value;
  refreshDiy();
  setStatus(QStringLiteral("自定义值已写入"));
}

void QtMainWindow::refreshFatal() {
  auto session = session_.session();
  if (!session)
    return;
  auto selected = ui_->listview_mk->currentRow();
  QSignalBlocker blocker(ui_->listview_mk);
  ui_->listview_mk->clear();
  const auto profile = session_.profile();
  auto count = profile.limit("fatal_count", 80);
  uint32_t namesAddress = 0;
  uint32_t nameWidth = 11;
  uint32_t nameStride = 16;
  switch (profile.version) {
  case EngineVersion::V65:
  case EngineVersion::V66:
  case EngineVersion::V64:
    namesAddress = 0x004A7510;
    break;
  case EngineVersion::V63MP:
  case EngineVersion::V63:
    namesAddress = 0x004FF551;
    count = std::min<uint32_t>(count, 36);
    break;
  case EngineVersion::V62:
  case EngineVersion::V61:
    namesAddress = 0x004FF710;
    count = std::min<uint32_t>(count, 36);
    break;
  default:
    break;
  }
  Bytes dataFile;
  if (profile.version == EngineVersion::V66) {
    try {
      auto path = session_.process().path;
      path = path.substr(0, path.find_last_of(L"\\/")) + L"\\Data.e5";
      dataFile = readFile(path);
      constexpr size_t offset = 59540;
      if (dataFile.size() < offset + size_t(count) * 16)
        dataFile.clear();
    } catch (...) {
      dataFile.clear();
    }
  }
  for (uint32_t id = 0; id < count; ++id) {
    QString name;
    if (!dataFile.empty()) {
      name = legacyTableText(dataFile, 59540 + size_t(id) * 16, nameWidth);
    } else if (namesAddress) {
      try {
        name = gameText(session,
                        checkedAddress(namesAddress,
                                       uint64_t(id) * nameStride, nameWidth),
                        nameWidth);
      } catch (...) {
      }
    }
    if (name.isEmpty() || name == QStringLiteral("非法字符"))
      name = QStringLiteral("必杀%1").arg(id);
    auto *item = new QListWidgetItem(QStringLiteral("%1:%2").arg(id).arg(name));
    item->setData(Qt::UserRole, id);
    ui_->listview_mk->addItem(item);
  }
  if (ui_->listview_mk->count())
    ui_->listview_mk->setCurrentRow(
        std::clamp(selected, 0, ui_->listview_mk->count() - 1));
  blocker.unblock();
  loadFatal();
}

void QtMainWindow::loadFatal() {
  auto session = session_.session();
  auto *selected = ui_->listview_mk->currentItem();
  if (!session || !selected)
    return;
  auto id = size_t(selected->data(Qt::UserRole).toULongLong());
  auto record = FatalService(session, session_.profile()).read(id);
  const std::array<QComboBox *, 5> persons = {
      ui_->mk_input_1_1, ui_->mk_input_1_3, ui_->mk_input_1_5,
      ui_->mk_input_1_7, ui_->mk_input_1_9};
  const std::array<QTextEdit *, 5> levels = {
      ui_->mk_input_1_2, ui_->mk_input_1_4, ui_->mk_input_1_6,
      ui_->mk_input_1_8, ui_->mk_input_1_10};
  for (size_t i = 0; i < persons.size(); ++i) {
    selectData(persons[i], record.persons[i]);
    setText(levels[i], record.levels[i]);
  }
  setText(ui_->mk_input_1_11, record.value);
}

void QtMainWindow::saveFatal() {
  auto session = session_.session();
  auto *selected = ui_->listview_mk->currentItem();
  if (!session || !selected)
    throw Error("未选择必杀记录");
  auto id = size_t(selected->data(Qt::UserRole).toULongLong());
  FatalRecord record{};
  const std::array<QComboBox *, 5> persons = {
      ui_->mk_input_1_1, ui_->mk_input_1_3, ui_->mk_input_1_5,
      ui_->mk_input_1_7, ui_->mk_input_1_9};
  const std::array<QTextEdit *, 5> levels = {
      ui_->mk_input_1_2, ui_->mk_input_1_4, ui_->mk_input_1_6,
      ui_->mk_input_1_8, ui_->mk_input_1_10};
  for (size_t i = 0; i < persons.size(); ++i) {
    record.persons[i] = uint16_t(persons[i]->currentData().toUInt());
    record.levels[i] =
        uint8_t(editUnsigned(levels[i], 0xff, "必杀领悟等级"));
  }
  record.value =
      uint8_t(editUnsigned(ui_->mk_input_1_11, 0xff, "必杀效果值"));
  FatalService(session, session_.profile()).write(id, record);
  loadFatal();
  setStatus(QStringLiteral("必杀记录已保存"));
}

void QtMainWindow::toggleControl(int index) {
  auto session = session_.session();
  const std::array<QCheckBox *, 9> controls = {
      ui_->checkBox_1, ui_->checkBox_2, ui_->checkBox_3,
      ui_->checkBox_4, ui_->checkBox_5, ui_->checkBox_6,
      ui_->checkBox_7, ui_->checkBox_8, ui_->checkBox_9};
  if (!session) {
    controls[size_t(index)]->setChecked(false);
    return;
  }
  auto profile = session_.profile();
  auto checked = controls[size_t(index)]->isChecked();
  if (index == 8) {
    if (checked)
      autoLifeTimer_.start();
    else
      autoLifeTimer_.stop();
    return;
  }
  if (index == 7) {
    auto count = profile.limit("battle_ours", 20) +
                 profile.limit("battle_friends", 40);
    auto base = profile.address("battle_units", 0x004A7B20);
    auto stride = profile.limit("battle_stride", profile.is66() ? 0x30 : 0x24);
    for (uint32_t slot = 0; slot < count; ++slot) {
      auto at = checkedAddress(base, uint64_t(slot) * stride, stride);
      if (session->u(at, 2) != 0xffff)
        session->writeU(at + 0x0e, checked ? 1 : 7, 1, false);
    }
    controlState_[size_t(index)] = checked;
    return;
  }
  if (index == 6) {
    LegacyAutoReturnPatch::set(session, profile, checked);
    controlState_[size_t(index)] = checked;
    return;
  }
  std::vector<ResolvedPatch> patches;
  try {
    RuntimeModuleImage image(session_.process().path, session);
    patches = ProfileRegistry().resolvePatches(image, kPatchKeys[index]);
  } catch (const std::exception &error) {
    throw Error(std::string("当前画像没有该控制项的签名配方: ") +
                error.what());
  }
  if (patches.empty())
    throw Error("当前画像没有该控制项的旧版地址配方");
  PatchManager manager(session);
  for (const auto &patch : patches) {
    auto address = session->actual(patch.address);
    if (checked)
      manager.apply(address, patch.original, patch.replacement);
    else
      manager.restore(address, patch.replacement, patch.original);
  }
  controlState_[size_t(index)] = checked;
}

bool QtMainWindow::attachPid(uint32_t pid, bool readOnly) {
  struct Context {
    DWORD pid = 0;
    HWND window = nullptr;
  } context{pid, nullptr};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto *context = reinterpret_cast<Context *>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner != context->pid || !IsWindowVisible(window))
          return TRUE;
        wchar_t title[256]{};
        GetWindowTextW(window, title, int(std::size(title)));
        if (!title[0])
          return TRUE;
        context->window = window;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&context));
  if (!context.window) {
    setStatus(QStringLiteral("未找到目标进程窗口"));
    return false;
  }
  releaseControlPatches();
  stopRecurringOperations();
  auto generation =
      session_.attach(context.window, selectedVersionHint(), readOnly);
  uiGeneration_ = std::max<uint64_t>(uiGeneration_, generation);
  return true;
}

Json QtMainWindow::diagnosticSnapshot() const {
  Json report;
  report["schema"] = "banshou-read-only-diagnostic-v1";
  report["operation"] = "attach-eight-pages";
  report["gameMemoryWrites"] = false;
  report["remoteCalls"] = false;
  auto session = session_.session();
  if (!session) {
    report["success"] = false;
    report["error"] = "尚未附加游戏";
    return report;
  }
  const auto profile = session_.profile();
  report["success"] = true;
  report["pid"] = session->pid;
  report["readOnlyEnforced"] = session->readOnly;
  report["sessionGeneration"] = session_.generation();
  report["profile"] = {
      {"version", int(profile.version)},
      {"displayVersion", profile.displayVersion},
      {"variant", profile.variantName},
      {"identity", profile.identity},
      {"limits", profile.limits},
      {"addresses", profile.addresses},
  };
  report["recordCounts"] = {
      {"person", ui_->listview_data->count()},
      {"deployedBattleUnits", ui_->listview_war->count()},
      {"warehouse", ui_->listview_item->count()},
      {"consumables", ui_->listview_item_2->count()},
      {"effects", ui_->listview_item_3->count()},
      {"fatal", ui_->listview_mk->count()},
      {"diy", ui_->listview_diy->rowCount()},
  };
  report["pages"] = Json::array();
  for (int page = 0; page < 8; ++page)
    report["pages"].push_back(
        {{"page", page}, {"success", true}});
  return report;
}

int runWindow(int argc, wchar_t **argv) {
  std::vector<std::string> arguments;
  arguments.reserve(size_t(std::max(argc, 1)));
  arguments.push_back("banshou");
  for (int i = 1; i < argc; ++i)
    arguments.push_back(utf8(argv[i]));
  std::vector<char *> raw;
  raw.reserve(arguments.size());
  for (auto &argument : arguments)
    raw.push_back(argument.data());
  int qtArgc = int(raw.size());
  char **qtArgv = raw.data();
  // 高 DPI 必须在 QApplication 之前开启：manifest 已声明 dpiAware，若不开启
  // 缩放，Qt 只放大字号而不放大控件几何，125%/150% 下按钮文字会被裁切。
  // 舍入策略用 PassThrough，避免 125% 被取整成 100%/150% 造成布局跳变。
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
  QApplication application(qtArgc, qtArgv);
  // 统一默认字体：未显式声明字体的控件此前会继承系统字体（125% 缩放下相当于
  // 12pt），与设计基线 9pt 混排，导致同一页字号参差且文字被裁。
  QApplication::setFont(QFont(QStringLiteral("微软雅黑"), 9));
  QCoreApplication::setApplicationName(QStringLiteral("扳手"));
  QCoreApplication::setOrganizationName(QStringLiteral("CaoCaoToolbox"));

  std::optional<uint32_t> attachPidValue;
  std::wstring startupSmokePath, diagnosticPath;
  bool startupSmokeExit = false, diagnosticExit = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring option = argv[i];
    const auto next = [&](std::wstring &value) {
      if (i + 1 >= argc)
        return false;
      value = argv[++i];
      return true;
    };
    if (option == L"--attach-pid") {
      std::wstring value;
      if (!next(value))
        return 2;
      try {
        auto parsed = std::stoul(value);
        if (!parsed || parsed > 0xFFFFFFFFull)
          return 2;
        attachPidValue = uint32_t(parsed);
      } catch (...) {
        return 2;
      }
    } else if (option == L"--startup-smoke-output") {
      if (!next(startupSmokePath))
        return 2;
    } else if (option == L"--startup-smoke-exit") {
      startupSmokeExit = true;
    } else if (option == L"--diagnostic-output") {
      if (!next(diagnosticPath))
        return 2;
    } else if (option == L"--diagnostic-exit") {
      diagnosticExit = true;
    }
  }
  if (!diagnosticPath.empty() && !attachPidValue)
    return 2;
  if (diagnosticExit && diagnosticPath.empty())
    return 2;
  if (startupSmokeExit && startupSmokePath.empty())
    return 2;
  if (!startupSmokePath.empty() && attachPidValue)
    return 2;

  QtMainWindow window;
  window.show();
  if (!startupSmokePath.empty()) {
    QTimer::singleShot(250, &application, [&window, startupSmokePath,
                                           startupSmokeExit, &application] {
      Json report;
      report["schema"] = "banshou-startup-smoke-v1";
      report["success"] = window.isVisible();
      report["gameMemoryWrites"] = false;
      report["remoteCalls"] = false;
      report["windowCreated"] = window.isVisible();
      report["qtPlatform"] = QApplication::platformName().toStdString();
      report["pages"] = Json::array();
      for (int page = 1; page <= 8; ++page)
        report["pages"].push_back("widget_" + std::to_string(page));
      int code = report["success"].get<bool>() ? 0 : 5;
      try {
        writeText(startupSmokePath,
                  report.dump(2) + "\n");
      } catch (...) {
        code = 5;
      }
      if (startupSmokeExit || code)
        application.exit(code);
    });
  } else if (attachPidValue) {
    QTimer::singleShot(0, &window, [&window, pid = *attachPidValue,
                                    diagnostic = !diagnosticPath.empty()] {
      try {
        window.attachPid(pid, diagnostic);
      } catch (...) {
      }
    });
    if (!diagnosticPath.empty()) {
      QTimer::singleShot(3000, &application, [&window, diagnosticPath,
                                              diagnosticExit, &application] {
        Json report;
        int code = 2;
        try {
          for (int page = 0; page < 8; ++page)
            window.showPageForTests(page);
          report = window.diagnosticSnapshot();
          code = report.value("success", false) ? 0 : 2;
        } catch (const std::exception &error) {
          report["schema"] = "banshou-read-only-diagnostic-v1";
          report["success"] = false;
          report["error"] = error.what();
        } catch (...) {
          report["schema"] = "banshou-read-only-diagnostic-v1";
          report["success"] = false;
          report["error"] = "诊断过程中出现未知异常";
        }
        try {
          writeText(diagnosticPath, report.dump(2) + "\n");
        } catch (...) {
          code = 3;
        }
        if (diagnosticExit || code)
          application.exit(code);
      });
    }
  }
  return application.exec();
}

} // namespace banshou
