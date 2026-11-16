# Python → C++ 功能对齐核查（2026-11-16）

核查范围：`老版游戏制作工具\扳手`（16 个 Python 模块、27 个测试文件、399 个用例）
逐项对照 `工具整合包\Banshou.Native`（`src/`、`include/`、`ui/`、`tests/`）。

结论分四类：**真缺口（本轮已修）**、**待确认**、**已用别的方式实现**、**有意为之**。

## 一、真缺口（本轮已修）

| # | 问题 | 位置 | 修法 |
|---|---|---|---|
| 1 | 战场保存每次都无条件调用 `changeWeather`，x/y 变化还调用 6.6 未验证 ABI 的 `changePosition` | `qt_mainwindow.cpp` `saveBattle` | 删除两处调用；方向刷新改为独立条件；`RemoteServices::changePosition` 对 6.6 直接抛错 |
| 2 | 区域灭敌不过滤已消失单位、逐槽写入失败无法整体回滚 | `killBattleArea` | 加 `visibility >= 2` 过滤；新增 `BattleService::writeRows` 单事务提交 |
| 3 | 武器槽允许写 70..149，可把防具/辅助写进武器位 | `savePerson` | 武器 `0..69`、防具 `70..108`、辅助 `109..end`，与 `mywindow.py` 一致 |
| 4 | 能力重算后无后置校验，调用约定漂移会静默改坏人物 | `recalculatePerson` | 复刻 Python：比对 HP/MP 上限、基础五维、兵种、等级、功勋、经验，异常即报错 |
| 5 | 附加只探测一次，加壳/延迟初始化基底首次附加易失败 | `session.cpp` `attach` | 500ms × 60 次退避重试，可被 detach 取消 |
| 6 | `DIY.csv` 只查 exe 目录 | `initializeAttachedUi` | exe 目录 → 用户数据目录 → 当前目录，首次运行复制到用户目录 |
| 7 | 仓库行显示 `ID:名称 等级=x 经验=y`，与 Python 的纯名称不一致，且缺「无效编号」分支 | `refreshWarehouse` | 改为 `ID：名称`，补 `无效编号：N` 与 `空` 三个分支 |
| 8 | 控制页第 9 项标签是「省略攻击动作」，实际行为却是自动复活 | `ui/mainwindow.ui` | 改名为「我军自动复活」，与 Python 一致 |

顺带修正的文案/几何：仓库 `item_7` 标签写成「忠奸度」（应为功勋池）、战场
`区域全灭`（应为区域灭敌）、五处控件互相遮挡、122 个控件未设字号导致 12pt/9pt 混排。

## 二、待确认（不改）

| 项 | 说明 |
|---|---|
| 战场「全部」下拉取值 | Python 写 `2*idx-1`（1/3/5），与其单项三态下拉（1/3/6）自相矛盾，疑为 Python 笔误；C++ 统一 1/3/6 |

## 三、已用别的方式实现（不改）

| 项 | C++ 实现 |
|---|---|
| 运行时物品目录补全（`runtime_catalog.py`） | `populateSharedCatalogs` 先读 Data.e5/Star.e5，再回退运行时表，失败时显示「物品N」 |
| 跨页扫描（`runtime_regions.py`） | `RuntimeModuleImage` 统一提供模块镜像，不再逐页记录空洞 |
| `.cczbool` 归档 | `VariableService` 的导入导出字节级一致 |
| 证据采集六个子命令 | `evidence.cpp` + `--evidence-*`，6.5 样本产出 25 个 bundle |
| 变量页四个右键动作 | `setupBooleanActions`，`qt_ui_contracts` 断言文案与 tooltip |
| 仓库批量按钮（全宝物/全道具/清空） | `WarehouseService::addAllTreasures/setAllConsumables/clearWarehouse` |

## 四、有意为之（详见 `deviations.md`）

能力门控全放开（上一轮需求）、三锚点语义证明、6.6 坐标刷新、固定 700×530 绝对定位、
`.ui` 与 `untitled.ui` 不再字节相同。

## 五、性能

| 改动 | 收益 |
|---|---|
| 附加只预热人物页，其余页面切页时按需刷新 | 少约 1300 次远程读、少建 6000 多个表格项（启动卡顿主因） |
| 仓库页 200 槽一次读 600 字节 | 600 次读 → 1 次（失败自动回退逐条） |
| 仓库/变量/DIY 列表关闭重绘后再填充 | 消除滚动条抖动与重排 |

## 六、未做

- 变量页与 DIY 页的批量读：这两页现在只在切页时加载，单次开销已可接受，暂不改造。
- `--diagnostic` 的 `compatibility` 段与退出码 4：诊断路径的价值低于上述修复，未纳入本轮。
