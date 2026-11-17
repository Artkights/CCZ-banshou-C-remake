# 界面布局（Win32 原生版）

## 1. 真值源

`src/win32_control_table.inc` 是界面几何、文案、字号、页归属的唯一运行期数据源，
由 `tools/gen_control_table.py` 从原设计稿生成后入库。共 315 条控件描述（含
`widget_0..widget_8` 九个页面容器）与 8 个工具栏按钮，合计 323 个子窗口。

## 2. 关键约定

- **客户区固定 700×530**：`AdjustWindowRectEx` 反算外窗尺寸，`WM_GETMINMAXINFO` 锁定。
- 页面容器位于客户区 `(9,77)`，尺寸 `681×391`；页面内控件坐标是相对容器的。
- 字体：`LOGFONT` + `MulDiv(pt, LOGPIXELSY, 72)`，微软雅黑 9pt 为主，少量 8/10pt。
- `app.manifest` 声明 `dpiAware=true`，不再需要框架级 DPI 缩放开关。
- 原设计稿里的 104 个右对齐富文本标签被还原为 `SS_RIGHT` + 剥标签后的纯文本；
  只有字号差异，没有真正的富文本排版。

## 3. 自动门禁

`tests/win32_ui_tests.cpp`（CTest `win32_ui_contracts`）断言：

1. 客户区 700×530，窗口不可调整大小；
2. 子窗口不少于 319 个，8 个工具栏按钮齐全；
3. 7 个动态控件与 3 个基线控件的矩形逐像素一致；
4. `checkBox_1` = 「可控制敌军和友军」、`checkBox_9` = 「我军自动复活」、
   `item_7` 含「功勋池」、无「省略攻击动作」残留；
5. `GetTextExtentPoint32W` 度量文字装得下；三对已知重叠控件不再相交；
6. `war_input_51..54` 默认隐藏；8 页切换当前页正确。

启动烟测与发布校验不依赖界面框架：只要 `--startup-smoke-*` 产出同 schema 的
JSON 与退出码即可，CLI 契约保持不变。
