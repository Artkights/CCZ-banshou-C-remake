# 与 Python 基线的有意偏差

本文件记录「看起来像缺口、但实际上是刻意为之」的差异。修改这些行为前请先读完
对应条目，否则很容易把上一轮明确要求的设计改回去。

## D1 能力门控只做「全放开」

`QtMainWindow::applyCapabilities()`（`src/qt_mainwindow.cpp`）目前只有 `enableAll()`：
把 7 个控制复选框和 9 个保存类控件全部置为可用，不按 capability 状态禁用。

原因：2026-11-16 的需求是「全面取消只读限制，所有内容均可读写」，此前按能力禁用
控件的逻辑已被整体移除。Python 版的 `_apply_capabilities` 会按能力灰度启用控件，
那是旧口径，**不要回补**。

影响：附加到不支持的基底时，控件仍然可点，真正的拒绝发生在写入层（事务前的地址 /
签名 / 范围校验）。

## D2 不做三锚点语义证明

Python 的 `engine_layout.py` + `semantic_resolver.py` 用「初始化锚点 + 读取锚点 +
写入锚点」三种代码形态共同证明表指针、条数和步长，任一类缺失就降级为只读。

C++ 版没有这套机制，改用「静态表布局 + 运行时段长假设 + 事务前范围校验」。
契约映射里这两项已标为 `not-applicable:python-internal-abstraction`。

## D3 6.6 坐标刷新不实现

`turn_refresh_position` 在 Python 里固定为 `Unavailable`（`POSITION_ABI_NOT_VERIFIED`），
原 UI 也从不调用。C++ 版此前在 `saveBattle` 里调用它，本轮已删除，并在
`RemoteServices::changePosition` 里对 6.6 直接抛错。

## D4 战场「全部」下拉写 6 而不是 5

Python `mywindow.py` 的「全部」下拉用 `2*idx-1` 得到 1/3/5，但同一页的单项三态下拉
用 1/3/6，两者自相矛盾（疑为 Python 侧笔误）。C++ 统一取 1/3/6。

## D5 窗口固定 700×530，控件绝对定位

不使用 QLayout 自适应：`.ui` 里 300 多个控件都是绝对坐标，改成布局等于重写界面，
且会打破 `qt_ui_contracts` 里 10 个精确矩形断言与 Python `tests/test_ui.py` 的基线。
高 DPI 由 `AA_EnableHighDpiScaling` 统一解决（见 `docs/ui_layout.md`）。

## D6 `ui/mainwindow.ui` 不再与 `untitled.ui` 字节相同

早期版本的 `.ui` 是从未同步的 `untitled.ui` 直接复制的，因此两者 SHA256 相同。
本轮修正了控制页第 9 项文案（`省略攻击动作` → `我军自动复活`）、仓库页
`item_7` 标签（`忠奸度` → `功勋池`）、战场按钮文案（`区域全灭` → `区域灭敌`）
以及若干控件几何，因此**不再相同**。Python 仓库里的 `untitled.ui` 未改动
（`tests/test_ui.py` 会读取它）。
