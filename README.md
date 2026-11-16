# 扳手 Native C++

这是旧 Python 扳手的 C++17/Qt Widgets 迁移工程。Qt 层直接使用旧版
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
- CTest 14 项全部通过：`native_contracts`、`lifecycle`、`qt_ui_contracts`、
  `memory/profile/item/variable/transaction/evidence/cli/startup/coverage/
  verify_release/platform_contracts`。

详见 `docs/spec.md`、`docs/tasks.md`、`docs/python_cpp_migration_matrix.csv`。

## 构建与测试

```powershell
cmake -S . -B C:\banshou-build\cmake4d -G "Visual Studio 17 2022" -A Win32 `
  -DCMAKE_PREFIX_PATH=D:/Qt/5.14.2/msvc2017
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

单文件静态版是默认发布形态，只需一个 `dist\扳手.exe`，不附带任何 DLL。

首次发布需要一份静态 Qt（安装器不提供 Windows 静态包，需自行编译一次）：

```powershell
.\tools\build_static_qt.ps1 -Prefix F:\qt5-static
.\tools\build_release.ps1 -StaticQt -QtPrefix F:\qt5-static
.\tools\verify_release.ps1
```

- `build_static_qt.ps1`：用 MSVC v142 + x86 静态 CRT 编译 Qt 5.14.2（Core/Gui/
  Widgets 与 qwindows/qoffscreen 插件），输出 `lib\cmake\Qt5\Qt5Config.cmake`。
  `-VerifyOnly` 只校验工具链与源码；`-SdkVersion 10.0.19041.0` 可锁定 SDK。
- `build_release.ps1 -StaticQt`：`BANSHOU_SINGLE_EXE=ON`，动态 CRT 改为 `/MT`，
  链接 Qt 静态库与平台插件，并执行 `check_single_exe.ps1` 导入审计。
- `dist\release.json` 记录模式、EXE SHA256 与文件清单；`dist\verify-release.json`
  记录 PE32/x86、版本资源、SHA、399 契约、启动自检和 6.5 证据采集结果。
- 旧的便携动态包已归档到 `dist\_legacy-portable-dynamic-qt\`，仅作回退用途。

当前发布（2026-11-16，单文件静态版）：

- 模式：`single-exe-static`
- EXE：`dist\扳手.exe`，13,814,784 字节
- SHA256：`2356A3ADE41EF63A7A30CD0EE92928C17B7BD624B252EBB86790DEB32A442C2B`
- 依赖：仅系统 DLL（见 `verify-release.json` 的导入审计），无 Qt/VC 运行库

## 验收边界

自动合同覆盖画像、消耗品、物品布局、变量、事务回滚、证据采集、CLI、启动自检
和 Qt 控件合同；真实游戏内的 6.1–6.4 派生样本、远程调用 ABI 行为仍建议按
`docs/python_cpp_migration_matrix.csv` 的 `verified_by` 列做人工抽查。单文件静
态版已完成构建与导入审计，并在空目录中独立启动验证（八页、零内存写入）。
