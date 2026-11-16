#include "qt_mainwindow.hpp"
#include "ui_mainwindow.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QFontMetrics>
#include <QLabel>
#include <QRadioButton>
#include <QRect>
#include <QTextDocument>
#include <QWidget>
#include <array>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <iostream>

using namespace banshou;

static void require(bool value, const char *message) {
  if (!value) {
    std::cerr << message << std::endl;
    std::exit(1);
  }
}

int main(int argc, char **argv) {
  // 与主程序保持一致的高 DPI 配置：否则测试按物理像素度量，会把 125%/150%
  // 系统下的正常布局误判成文字装不下。
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
  QApplication application(argc, argv);
  QtMainWindow window;
  auto &ui = window.uiForTests();
  require(window.size().width() == 700 && window.size().height() == 530,
          "main window geometry differs from Python baseline");
  require(window.minimumSize() == window.maximumSize(),
          "main window is not fixed size");
  require(window.findChildren<QWidget *>().size() >= 319,
          "Python baseline controls are missing");
  require(ui.toolBar->actions().size() == 8,
          "toolbar does not contain all eight pages");
  require(window.findChild<QWidget *>(QStringLiteral("war_input_51")),
          "Python-only battle condition controls are missing");
  require(window.findChild<QWidget *>(QStringLiteral("var_save_3")),
          "Python-only variable save controls are missing");
  auto *variableList =
      window.findChild<QWidget *>(QStringLiteral("listview_var_1"));
  auto *variableSave =
      window.findChild<QWidget *>(QStringLiteral("var_save_1"));
  require(variableList && variableList->actions().size() == 4,
          "boolean context actions are missing from the variable list");
  require(variableSave && variableSave->actions().size() == 4,
          "boolean context actions are missing from the save button");
  const std::array<QString, 4> booleanActions = {
      QStringLiteral("保存选中行"), QStringLiteral("保存本页全部修改"),
      QStringLiteral("全量导出…"), QStringLiteral("全量导入并保存…")};
  for (size_t i = 0; i < booleanActions.size(); ++i) {
    require(variableList->actions()[int(i)]->text() == booleanActions[i],
            "boolean action label differs from Python");
    require(variableSave->actions()[int(i)]->text() == booleanActions[i],
            "boolean action label differs from Python");
  }
  require(variableSave->toolTip() ==
              QStringLiteral("点击保存选中行；右键可保存本页全部修改、全量导出或全量导入。"),
          "boolean save tooltip differs from Python");
  const std::array<std::pair<const char *, QRect>, 7> conditionGeometry = {{
      {"war_input_51", QRect(185, 170, 25, 25)},
      {"war_input_52", QRect(255, 170, 25, 25)},
      {"war_input_53", QRect(185, 200, 25, 25)},
      {"war_input_54", QRect(255, 200, 25, 25)},
      {"var_save_1", QRect(170, 20, 50, 25)},
      {"var_save_2", QRect(390, 20, 50, 25)},
      {"var_save_3", QRect(610, 20, 50, 25)},
  }};
  for (const auto &entry : conditionGeometry) {
    auto *widget = window.findChild<QWidget *>(QString::fromUtf8(entry.first));
    require(widget && widget->geometry() == entry.second,
            "a Python-only control geometry differs from baseline");
  }
  const std::array<std::pair<const char *, QRect>, 3> geometry = {{
      {"checkBox_1", QRect(20, 38, 171, 19)},
      {"listview_diy", QRect(10, 50, 661, 331)},
      {"war_life", QRect(550, 300, 121, 35)},
  }};
  for (const auto &entry : geometry) {
    auto *widget = window.findChild<QWidget *>(QString::fromUtf8(entry.first));
    require(widget && widget->geometry() == entry.second,
            "a baseline widget geometry differs from Python");
  }
  auto *control = window.findChild<QCheckBox *>(QStringLiteral("checkBox_1"));
  require(control && control->text() == QStringLiteral("可控制敌军和友军"),
          "baseline control text differs from Python");

  // 控制页第 9 项必须是「我军自动复活」：旧标签「省略攻击动作」从未实现，
  // Python 版已改名，这里同时保证旧文案不再残留在任何控件上。
  auto *reviveBox = window.findChild<QCheckBox *>(QStringLiteral("checkBox_9"));
  require(reviveBox && reviveBox->text() == QStringLiteral("我军自动复活"),
          "checkbox 9 is not labelled as auto revive");
  for (QWidget *widget : window.findChildren<QWidget *>()) {
    if (widget->objectName().isEmpty())
      continue;
    require(widget->property("text").toString() !=
                QStringLiteral("省略攻击动作"),
            "stale omit-attack-action label still present");
  }

  // 仓库页全局字段标签：item_7 是功勋池，不能与 item_6 的忠奸度重复。
  auto *meritLabel = window.findChild<QLabel *>(QStringLiteral("item_7"));
  require(meritLabel && meritLabel->text().contains(QStringLiteral("功勋池")),
          "warehouse merit pool label is wrong");

  // 仓库行文本纯函数：空槽、越界编号、正常编号三种形态。
  require(warehouseRowText(0xff, 255, QStringLiteral("x")) == QStringLiteral("空"),
          "warehouse empty slot text is wrong");
  require(warehouseRowText(5, 255, QStringLiteral("青龙偃月刀")) ==
              QStringLiteral("5：青龙偃月刀"),
          "warehouse row text is not id:name");
  require(warehouseRowText(300, 255, QStringLiteral("物品300")) ==
              QStringLiteral("无效编号：300"),
          "warehouse out-of-range text is wrong");

  // 每一个按键的文字都必须完整装进自己的矩形，且不能互相遮挡。
  std::vector<std::string> clipped;
  for (QWidget *widget : window.findChildren<QWidget *>()) {
    if (widget->isHidden())
      continue;
    const QFontMetrics metrics(widget->font());
    const auto report = [&](const char *what, const QString &text, int need,
                            int have) {
      std::ostringstream line;
      line << what << ": " << widget->objectName().toStdString()
           << " text=[" << text.toStdString() << "]"
           << " advance=" << metrics.horizontalAdvance(text)
           << " need=" << need << " have=" << have
           << " font=" << widget->font().family().toStdString() << "/"
           << widget->font().pointSizeF() << " h=" << metrics.height();
      clipped.push_back(line.str());
    };
    if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
      const auto text = button->text();
      if (text.isEmpty())
        continue;
      const bool checkable = qobject_cast<QCheckBox *>(button) ||
                             qobject_cast<QRadioButton *>(button);
      const int padding = checkable ? 19 : 14;
      const int needWidth = metrics.horizontalAdvance(text) + padding;
      if (needWidth > button->width())
        report("button too narrow", text, needWidth, button->width());
      if (metrics.height() - 2 > button->height())
        report("button too short", text, metrics.height() - 2, button->height());
    } else if (auto *label = qobject_cast<QLabel *>(widget)) {
      auto text = label->text();
      if (text.isEmpty())
        continue;
      if (text.contains(QLatin1Char('<'))) {
        QTextDocument document;
        document.setHtml(text);
        text = document.toPlainText();
        // 富文本转纯文本会带上段落分隔符，度量前必须去掉，否则宽算会虚高。
        text.remove(QChar(0x2029));
        text.remove(QChar(0x2028));
        text.remove(QLatin1Char('\n'));
      }
      const int needWidth = metrics.horizontalAdvance(text) + 2;
      if (needWidth > label->width())
        report("label too narrow", text, needWidth, label->width());
      if (metrics.height() - 2 > label->height())
        report("label too short", text, metrics.height() - 2, label->height());
    }
  }
  if (!clipped.empty()) {
    for (const auto &line : clipped)
      std::cerr << line << std::endl;
    std::exit(1);
  }

  // 修复前被遮挡的三对控件：几何上不再相交。
  const std::array<std::pair<const char *, const char *>, 3> noOverlap = {{
      {"var_input_4", "var_save_1"},
      {"mk_1_11", "mk_input_1_11"},
      {"war_input_28", "war_30"},
  }};
  for (const auto &pair : noOverlap) {
    auto *left = window.findChild<QWidget *>(QString::fromUtf8(pair.first));
    auto *right = window.findChild<QWidget *>(QString::fromUtf8(pair.second));
    require(left && right, "overlap probe widget is missing");
    require(!left->geometry().intersects(right->geometry()),
            "two controls still overlap each other");
  }
  // 扩展状态位输入框默认隐藏，避免盖住 4 个状态复选框。
  auto *conditionDigit =
      window.findChild<QWidget *>(QStringLiteral("war_input_51"));
  require(conditionDigit && conditionDigit->isHidden(),
          "extended condition inputs must start hidden");

  for (int page = 0; page < 8; ++page) {
    window.showPageForTests(page);
    require(window.currentPageForTests() == page, "page action mismatch");
  }
  std::cout << "qt ui contracts passed" << std::endl;
  return 0;
}
