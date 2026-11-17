# 扳手 Native C++

这是旧 Python 扳手的 C++17/Win32 原生 迁移工程。Win32 层直接使用旧版
`untitled.ui`，保持 700x530 固定窗口、八个工具栏入口、原对象名和动态控件；
核心层保留 Unicode 路径访问、x86 PE 画像、会话 generation、内存读写、远程
线程生命周期和旧版直写顺序。

## 迁移结论（2026-11-16）

C++ 侧已完成与 Python 版的功能、UI、写安全等价迁移：

- 八页 UI：控制、人物、战场、仓库、天赋、变量、DIY、必杀；保留 Python 动态
  控件 `war_input_51..54`、`var_save_1..3` 及四项布尔右键操作。
- 画像：6.1–6.6；6.6 标准 352 消耗品槽、6.4/6.5 105 槽、6.1–6.3 43 槽分段、
  单 EXE 256 槽；物品 255/256/512 行；6.6 布尔 1536 可写/2048 浏览、481 指针。
- 写安全：人物、战场、仓库、天赋、专属/套装、必杀全部改为“快照 + 比较交换 +
  回滚”，未触碰字节保留原值。
- 证据：`--evidence-static/oracle/runtime/inspect/verify/manifest` 与 Python
  `evidence_collector.py` 的 bundle schema 1.0、规范化摘要和清单兼容；运行时
  采集已用断点进程实测。
- 契约：`resources/baseline.json` 的 399 条 Python 测试全部有终态
  （`covered:<ctest>` 或 `not-applicable:<reason>`），`coverage_contracts`
  强制校验，不允许 `pending`。
- CTest 14 项全部通过：`native_contracts`、`lifecycle`、`win32_ui_contracts`、
  `memory/profile/item/variable/transaction/evidence/cli/startup/coverage/
  verify_release/platform_contracts`。

详见 `docs/spec.md`、`docs/tasks.md`、`docs/python_cpp_migration_matrix.csv`。

## 构建与测试

```powershell
cmake -S . -B C:\banshou-build\cmake4d -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_PREFIX_PATH=D:/Win32/5.14.2/msvc2017
cmake --build C:\banshou-build\cmake4d --config Release --parallel 2
ctest --test-dir C:\banshou-build\cmake4d -C Release --output-on-failure
```

契约状态在 Python 源码或映射表变化后重算：

```powershell
python tools\apply_contract_status.py
```

## 证据采集器

```powershell
扳手.exe --evidence-static --exe Ekd5.exe --output .\evidence
扳手.exe --evidence-oracle --exe Ekd5.exe --output .\evidence --legacy-index 1
扳手.exe --evidence-runtime --pid 1234 --feature runtime_probe --output .\evidence `
  --breakpoint 0x4012D0 --samples 2 --recipe recipe.json --tag phase=test
扳手.exe --evidence-verify <bundle.json> --output .\evidence
扳手.exe --evidence-manifest --output .\evidence
```

bundle 的 `bundleDigest`、`rawSampleDigest`、`manifest.json` 与 Python
`capability_evidence.py` 使用同一 canonical JSON 规则，可与 Python 证据互验。

## 发布

默认且唯一的发布形态是 `single-exe-native`：一个 `dist\扳手.exe`，不附带任何 DLL，
不依赖 Qt / VC 运行库，也不再需要任何构建期第三方框架。

构建命令：

    tools/build_release.ps1
    tools/verify_release.ps1

构建只需要 MSVC v142（Win7 兼容）与 Windows SDK；控件几何由
`src/win32_control_table.inc` 提供，该文件由 `tools/gen_control_table.py`
从原设计稿生成后入库，运行期不依赖 Python 或设计稿。

- `dist\release.json` 记录模式、EXE SHA256 与文件清单；`dist\verify-release.json`
  记录 PE32/x86、版本资源、SHA、399 契约与启动自检结果。
- `tools/check_single_exe.ps1` 审计导入表，禁止 Qt/VC 运行库与外部 DLL。

当前发布（2026-11-17，单文件原生版）：

- 模式：`single-exe-native`
- EXE：`dist\扳手.exe`，2,723,840 字节
- SHA256：`CB4EF8E98B322EF34040C86C49920457628FD8DFF1520A31E757C45742087A9F`
- 导入：BCRYPT / COMCTL32 / COMDLG32 / DBGHELP / GDI32 / KERNEL32 / OLE32 / SHELL32 / USER32 / VERSION

## 界面与功能覆盖

界面按原设计稿严格复刻：700×530 客户区、工具栏 8 页、315 条控件描述（含 9 个页面容器）
加 8 个工具栏按钮，坐标、文案与字号逐项对齐；`win32_ui_contracts` 对几何、
文案、遮挡与隐藏状态做自动断言。

当前已接通的交互：窗口创建与 8 页切换、附加进程（`--attach-pid`）、控制页 9 项
（补丁 / 托管 / 自动回归 / 自动复活标记）、仓库页刷新与单槽保存、启动烟测与诊断 JSON。

其余五页的业务编排已按控件名逐项接入：人物（38 字段 + 装备分类 + 能力重算）、
战场（44 字段 + 策略/目标/状态位 + 复活/灭敌/锁定/连接人物）、天赋（4+2/3+1 两套布局）、
变量（3 组 100 行 + 布尔右键四动作 + CCZBOOL1 归档）、自定义（DIY.csv 三级路径 + 1000 行滚动）、
必杀（5 人 + 等级 + 效果值）。两个 1 秒定时器负责锁定写回与自动复活。

## 验收边界

自动合同覆盖画像、消耗品、物品布局、变量、事务回滚、证据采集、CLI、启动自检
和 Win32 控件合同；真实游戏内的 6.1–6.4 派生样本、远程调用 ABI 行为仍建议按
`docs/python_cpp_migration_matrix.csv` 的 `verified_by` 列做人工抽查。
