# 原生迁移验收记录

## 2026-11-16 全量迁移验收

### 自动合同（CTest 14/14）

| 测试 | 结果 | 覆盖 |
|---|---|---|
| native_contracts | 通过 | 编码、PE、人物、仓库、变量、DIY、补丁原字节校验 |
| lifecycle | 通过 | 1,000 次附加/解绑、代际保护 |
| win32_ui_contracts | 通过 | 700x530、八页、动态控件几何、四项布尔右键动作 |
| memory_contracts | 通过 | 引用/实际地址、只读拒绝、事务回滚、比较交换拒绝 |
| profile_contracts | 通过 | 6.1–6.6 画像、消耗品、布尔/指针、物品 255/512 |
| item_contracts | 通过 | 仓库 200 槽、105/43/352 消耗品、全宝物过滤 |
| variable_contracts | 通过 | 位集 MSB0、DWORD 数组、481 指针、归档校验 |
| transaction_contracts | 通过 | 人物/战场/仓库未知字节保留、第二条目失败回滚 |
| evidence_contracts | 通过 | canonical 摘要、篡改拒绝、static/oracle/runtime/verify |
| cli_contracts | 通过 | 证据子命令参数与退出码 |
| coverage_contracts | 通过 | 399 条契约无 pending，套件/理由码白名单 |
| verify_release_contracts | 通过 | EXE SHA 记录与验证脚本存在 |
| platform_contracts | 通过 | 中文路径、SHA256、PE 资源、地址溢出 |
| startup_contracts | 通过 | `banshou-startup-smoke-v1`、八页、零内存写入 |

### 契约映射

- `resources/baseline.json`：399 条，`pending=0`。
- `covered:*` 382 条，`not-applicable:*` 17 条：
  - `test_build_release.py`（7）：Python 构建脚本内部。
  - `test_engine_layout.py`（6）与 `test_semantic_resolver.py`（4）：Python
    内部抽象；边界/指针不变量由 `profile_contracts` 覆盖。
- 映射表：`docs/native_contract_map.csv`；应用脚本：
  `tools/apply_contract_status.py`。

### 真实样本

- 6.5：`20260820_104551_加强版6.5未加密版\Ekd5.exe`
  （SHA256 `139E2A5A...`）→ 6.5、105 消耗品、255 物品、17 字节名称、
  4096 布尔数组、481 指针。
- 6.6：`Ccz66ItemIconSmoke_20260820_084836\Ekd5.exe`
  （SHA256 `4A4FD8DD...`）→ 6.6、352 消耗品（基址 `0x493AF0`）、256 物品、
  1536/2048 布尔、481 指针。
- `--evidence-static` 为 6.5 生成 25 个 bundle；`--evidence-oracle` 生成
  `legacy_oracle` bundle；运行时采集对断点进程采集并通过 `--evidence-verify`
  与 `--evidence-inspect`（退出码 0）。
- 证据 bundle 的 `bundleDigest` 已用 Python `capability_evidence.sha256_json`
  交叉复算一致。

### 发布

- 模式：`single-exe-native`（本机无静态 Win32，改用 Win32 原生 源码自行编译）。
- 静态 Win32：`F:\qt5-static`，MSVC v142 + x86 + `-static-runtime`（`/MT`），
  含 `Win32::QWindowsIntegrationPlugin` 与 `Win32::QOffscreenIntegrationPlugin`；
  构建流程固化在 `tools\（已移除的静态 Qt 构建脚本）`。
- `dist\扳手.exe`：2,723,840 字节，SHA256
  `CB4EF8E98B322EF34040C86C49920457628FD8DFF1520A31E757C45742087A9F`。
- 导入审计（`tools\check_single_exe.ps1`）：PE32/x86、最低系统 6.1，仅
  ADVAPI32/BCRYPT/DBGHELP/DWMAPI/GDI32/IMM32/KERNEL32/NETAPI32/OLE32/
  OLEAUT32/SHELL32/USER32/USERENV/UXTHEME/VERSION/WINMM/WS2_32/WTSAPI32，
  无 Win32*.dll、VCRUNTIME140、MSVCP140、CONCRT140、UCRTBASE。
- 空目录独立运行：仅复制 `扳手.exe` 到空目录，`--startup-smoke-*` 返回 0，
  `qtPlatform=windows`、八页齐备、`gameMemoryWrites=false`。
- `dist\release.json` 记录模式、SHA 与单文件清单；`dist\verify-release.json`
  为 `success=true`：PE32/x86、版本资源、SHA、399 契约终态、启动自检、
  6.5 证据 25 bundles。
- 旧 `portable-dynamic-qt` 产物归档至 `dist\_legacy-portable-dynamic-qt\`。
- `tools\（已移除的静态 Qt 构建脚本）`、`tools\build_release.ps1`、
  `tools\verify_release.ps1` 均已实际执行通过。

### 本轮修复的构建/打包缺陷

1. `check_single_exe.ps1`：`if (dumpbin 无输出) {...} else { throw }` 分支写错，
   即使找到 dumpbin 也会误报“两者都不可用”；改为顺序回退后统一判空。
2. `check_single_exe.ps1`：`dumpbin /imports` 输出并无 `DLL Name:` 前缀，原正则
   恒不匹配；改用 `/dependents` 并配套正则。
3. `check_single_exe.ps1` / `build_release.ps1`：VS 目录枚举用 `Get-ChildItem`，
   在受限 shell 下被过滤且漏查 `2022\Community` 层级；改用 `[IO.Directory]` API，
   并支持 `BANSHOU_MSVC_TOOLSET` 显式指定工具集。
4. `build_release.ps1`：`ErrorActionPreference=Stop` 下 CMake/MSBuild 的 stderr
   告警会被当成终止错误；新增 `Invoke-Native` 统一处理原生命令退出码。
5. `CMakeLists.txt`：单文件模式硬绑定 `Win32 5.15.2 EXACT`，本机只有 5.14.2；
   改为 `BANSHOU_QT_MIN_VERSION`（默认 5.14）可配置。
6. 新增 `tools\（已移除的静态 Qt 构建脚本）`：手工装配 MSVC 环境（不依赖 `VsDevCmd` 的
   `reg.exe`）、`-VerifyOnly` 预检、`-SdkVersion` 锁定 SDK。

### 验收边界

- 真实游戏内的控制补丁切换、自动回归、远程复活/转向/天气 ABI 行为未在本轮
  自动合同内做实机对拍；代码路径、签名定位、原字节校验与回滚已覆盖，建议按
  `docs/python_cpp_migration_matrix.csv` 人工抽查。
- 6.1–6.4 派生样本未在本机逐一提供；画像解析、43 槽分段、物品布局与签名拒绝
  逻辑已有单元合同覆盖。
- 单文件静态版已完成构建、导入审计与空目录启动验证；静态 Win32 为 5.14.2 自编译
  （Win32 5.15.2 静态包本机不可得），功能路径与动态版一致，未改变核心代码。
