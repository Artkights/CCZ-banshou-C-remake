# 界面布局与高 DPI

## 1. 为什么按钮文字会被裁

排查结论是三条叠加，而不是某几个按钮宽度算错（96 DPI 下 159 个静态控件的文字都装得下）：

1. **高 DPI 未启用（主因）**。`resources/app.manifest` 声明 `dpiAware=true`，系统因此
   不做位图拉伸；但 Qt5 默认不缩放控件几何，只按真实 DPI 放大字号。125% 时 19/20/25px
   高的控件纵向被裁，150% 时连宽度也不够。
2. **默认字体不一致**。`.ui` 中有 122 个控件只写了字族没写字号，会继承系统字体——
   在 125% 缩放的机器上等效 12pt，而其余控件是 9pt，同一页字号参差且 12pt 的那些撑破
   了自己的矩形。
3. **五处真实遮挡**：`var_save_1..3` 压住三个「跳转」按钮、`war_input_51..54` 盖住四个
   状态复选框、`mk_1_11` 压进输入框、天赋页初始态输入框叠罗汉、战场右侧单字标签被
   下拉框压住。

## 2. 改了什么

| 项 | 位置 |
|---|---|
| `AA_EnableHighDpiScaling` + `AA_UseHighDpiPixmaps` + `PassThrough` 舍入策略 | `src/qt_mainwindow.cpp` 的 `runWindow`，必须在 `QApplication` 之前 |
| 窗口与 centralwidget 默认字体 微软雅黑 9pt | `QtMainWindow` 构造函数 |
| 122 个字体块补 `pointsize 9` | `ui/mainwindow.ui` |
| 「跳转」按钮 102→48px（让位给「保存」） | `ui/mainwindow.ui` |
| 扩展状态位输入框默认隐藏，由 `loadBattle` 切换 | `createPythonOnlyControls` |
| `mk_1_11` 71→58px | `ui/mainwindow.ui` |
| 战场右侧六组下拉 61→56px、`war_input_9` 41→36px | `ui/mainwindow.ui` |
| `data_input_11` 91→21px | `ui/mainwindow.ui` |
| 天赋页标签 15→18px、`label_help` 16→20px | `ui/mainwindow.ui` |
| 初始化时调用 `updateEffectLayout()` | `setInitialState` |

## 3. 机器门禁

`tests/qt_ui_tests.cpp` 会遍历 8 个页面的所有按钮、勾选、单选与标签，逐项断言：

- 文字宽度 + 内边距 ≤ 控件宽度（按钮 14px、勾选/单选 19px、标签 2px）；
- 行高 - 2 ≤ 控件高度；
- 三对被修复的控件不再相交；
- 扩展状态位输入框默认隐藏。

任何新控件只要装不下文字，测试就会失败并打印 `objectName / 文本 / 需要宽度 / 实际宽度`。

**注意**：该测试必须用真实 `windows` 平台运行。`offscreen` 平台加载不到「微软雅黑」，
会退化成更宽的替代字体，把正常布局误判成截断（早期就是这样误报的）。CTest 已改成
默认平台，测试本身不调用 `show()`，不会弹窗。

## 4. 已知限制

- 窗口仍是固定 700×530 逻辑像素（与 Python 基线一致）。4K@200% 下窗口物理尺寸很大，
  若屏幕装不下，可通过设置环境变量 `QT_SCALE_FACTOR=1.25` 启动。
- 控件仍是绝对定位，不随窗口缩放重排；这是为了保住 10 个精确矩形断言与 Python 基线。
