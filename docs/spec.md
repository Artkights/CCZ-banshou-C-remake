# 扳手 Python→C++ 全量迁移收尾规格

## 1. 目标与验收定义

目标：在 `工具整合包/Banshou.Native` 内完成与 Python 版 `老版游戏制作工具/扳手` 的功能、UI、写安全等价迁移，形成可机器校验的“全量迁移”结论，并发布 C++ 版 Release。

“全量迁移”按以下四条同时成立判定：

1. **功能等价**：八页 UI、6.1–6.6 画像、消耗品 352/105/43/256 槽、物品表 255/256/512 行、6.6 位集布尔 1536 可写/2048 浏览、481 指针变量、特效 4+2 与 3+1、必杀 80×16、远程调用、控制补丁、6.6 自动回归、布尔归档、诊断 CLI 均可用。
2. **写入安全等价**：所有页面保存均为“读取快照 → 比较交换 → 失败回滚”；未知字节保留；指针/布局变化拒绝写入。
3. **契约等价**：`resources/baseline.json` 的 399 条 Python 测试逐条有 `nativeStatus`，只允许 `covered:<ctest>` 与 `not-applicable:<code>` 两种终态；新增 `coverage_contracts` CTest 强制校验，出现 `pending` 即失败。`not-applicable` 仅限 Python 构建脚本内部与 Python 内部抽象（见 §7）。
4. **发布可用**：`BANSHOU_SINGLE_EXE`（单文件静态版，Qt 由 `tools/build_static_qt.ps1`
   从源码编译并静态链接，含平台插件）；若静态 Qt 不可得，退回便携动态 Qt Release
   （EXE + Qt DLL + platforms + 运行库 + 说明），`verify_release.ps1` 全绿，并在包内与
   README/ACCEPTANCE 明确标注实际模式，不冒充单文件静态版。

## 2. 现状基线（已核实）

- 构建通过：`native_contracts`、`lifecycle`、`qt_ui_contracts` 3 项 CTest 全绿。
- `resources/catalog.json` schema 2：44 常量、33 签名、6 个 legacy 画像、base66、remoteCalls、patchLocators。
- `resources/baseline.json` schema 2：399 条契约，当前全部 `pending`。
- 证据 CLI 仅有 `--evidence <exe>` 静态摘要；Python `evidence_collector.py` 的 `static/oracle/runtime/inspect/verify/manifest` 未迁移。
- `ExclusiveEffectService::write` 从零构造 16 字节，未保留未知字节且非比较交换；`EffectService::write`、`FatalService::write`、`PersonService::write`、`WarehouseService`、`BattleService` 仍是非守卫写或仅异常回滚。
- `Capability` 只解析 `state/reason`，缺 `reason_code/recipe`，无法做能力门控和证据配方。
- Python `_setup_boolean_actions` 的“保存选中行 / 保存本页全部修改 / 全量导出 / 全量导入并保存”四项，C++ 仅实现后两项。
- 本机仅 Qt 5.14.2 动态版；静态 Qt 5.15.2 未安装。
- `window.cpp` 为未参与构建的旧 Win32 实现，不作为迁移对象。

## 3. 证据采集器（A）

### A1 `include/image.hpp` / `src/image.cpp`
新增：

```cpp
std::vector<uint32_t> findLiteralReferences(uint32_t value, bool executableOnly = true) const;
```

按可执行节（或全部节）原始字节搜索 4 字节小端立即数，返回 VA 列表，语义与 Python `PEImage.find_literal_references` 一致。

### A2 `include/profile.hpp` / `src/profile.cpp`
扩展能力模型并解析 catalog：

```cpp
struct Capability {
  bool readable = false, writable = false;
  std::string state, reason, reasonCode;
  Json recipe = Json::object();
};
```

- 解析 `capabilities[key].state/reason/reason_code/recipe`。
- 解析 `remoteCalls` 到 `remoteAddresses`（缺失时保留 catalog 缺省）。
- 增加 `Json capabilityRecipe(const std::string&) const` 的等价读取（可从 `capabilities` 直接取）。

### A3 `include/evidence.hpp` / `src/evidence.cpp`（主体）
实现与 Python `capability_evidence.py` + `evidence_collector.py` 等价的 C++ API：

- 常量：`EVIDENCE_SCHEMA_VERSION="1.0"`、`COLLECTOR_ID="wrench-evidence-collector/1"`、`MANIFEST_NAME="manifest.json"`。
- `canonicalJson(Json)`：转换为按键排序的 `nlohmann::json`，`dump(-1,' ',false)`；`digestJson()` 返回大写 SHA256。
- `recipeId(feature, recipe)`：`"ccz66.<feature>.<digestJson(recipe).substr(0,20)>"`。
- `requiredRuntimeChecks(feature)`：完整复制 Python `RUNTIME_REQUIRED_CHECKS` 五组与 `GENERIC_RUNTIME_CHECKS`。
- `makeBundle/finalizeBundle/validateBundle`：字段、摘要、运行时检查与 Python 完全一致；`validateBundle(runtimeRequired)` 抛 `Error`。
- `EvidenceRepository`：`writeBundle`（文件名 `<safeFeature>-<sha12>-<phase>.json`、缩进 2、末尾换行）、`rebuildManifest`（临时文件 + 原子替换）、`loadBundles`、`verifiedRecipe`。
- 反汇编：capstone x86-32；`disassemble(image,address,before,size)` 返回 `start/target/fileBacked/bytesHex/instructions/normalizedInstructions/normalizedFingerprint`；规范化规则与 Python `_normalize_instruction` 一致（`0x…` 与独立整数替换为 `<imm>`，压缩空白，小写）。
- `reconstructLegacyAutoReturnStub(original, legacyIndex, battleAddress, battleStride)`：逐字节复制 Python 覆盖表（`legacyIndex>=3` 与 `>=1` 分支）。
- `collectStatic(exe, output, features, recipePath)`：默认遍历全部能力；静态证据含 `profileState/reasonCode/addresses/windows/commonRiskFacts`；`commonRiskFacts` 使用 `0x004242B0/0x0044E570/0x0044EA74/0x00485E00/0x00457428/0x004594DD/0x00414F65`（before=8,size=32）与 `0x00510C80/0x004B09DB/0x004B5DF0` 字面引用计数。
- `collectLegacyOracle(exe, output, legacyIndex, battleAddress, battleStride)`：风险地址窗口、字面引用计数、`autoReturnStub`（原始/重构/差异偏移）。
- `collectRuntime(pid, ...)`：Toolhelp32 取模块；`Win32MemoryBackend` + `MemorySession`；`DebugActiveProcess` + 0xCC 断点 + `WaitForDebugEvent`；x86 `CONTEXT` 采样 Eax..Eip/EFlags、栈、watch（`session.actual` 解析引用）；单步恢复、重新布点、退出复原；支持 `--samples/--timeout/--stack-bytes/--recipe/--tag/--append`；样本附加 `tags.base`。
- `inspectBundle(path)`：打印 `feature/phase/digest`，缺失自动检查返回 2，完整验证失败返回 2，成功返回 0。
- `verifyCapturedBundle(path, output)`：按 `recipe.verificationContract.checks` 实现谓词 `min-samples/tag-values/field-present/all-equal/distinct-count/watch-relation/all-of`，全部通过后写入 `verification.status="verified"` 并复验。
- `rebuildManifest(output)`。

### A4 `src/main.cpp`
路由：`--evidence`（保留）、`--evidence-static`、`--evidence-oracle`、`--evidence-runtime`、`--evidence-inspect`、`--evidence-verify`、`--evidence-manifest` 全部交给 `evidenceCli`。

### A5 `tests/mock_target.cpp`
新增 `--breakpoint-spin` 模式：循环调用一个带符号的 `spin_tick()`，供 runtime 采集器真实附加采样。

## 4. 事务写入（B）

### B1 `src/game.cpp`
- `EffectService::write`：读取 `expected` 原始行 → 在 `expected` 上覆盖已知字段 → `guardedCompareExchangeTransaction({at, expected, replacement, true, false})`。
- `ExclusiveEffectService::write`：读取 `expected` → 仅覆盖 16 字节中的已知字段（person/item/value、set items/value）→ 守卫交换；未触碰字节保持原值。
- `FatalService::write`：读取 `expected` → 覆盖 5 组人物/等级与效果值 → 守卫交换。
- `PersonService::write`：读取人物行与 r/s/face/功勋快照 → 单次 `compareExchangeTransaction` 写入全部条目；任一失败回滚。
- `WarehouseService::writeRecord`：单条三字节比较交换；`writeConsumable`：单字节比较交换。
- `BattleService::write`：读取 `expected` 行 → 覆盖字段 → 守卫交换；`BattleStateService::write`：回合/天气/季节/关卡字段快照后单事务写入。
- `QtMainWindow::saveWarehouse`/`saveBattle` 的仓库、金钱、忠奸度、功勋池、战场状态合并为同页单事务（存在多地址时使用 `compareExchangeTransaction`）。

### B2 `include/game.hpp`
按需增加批量方法声明（如 `VariableService::writeBools(const std::vector<std::pair<size_t,bool>>&)`），不改变现有公开行为。

## 5. UI 对齐（C）

### C1 布尔页四项操作（`src/qt_mainwindow.cpp`）
- `setupBooleanActions()` 为 `listview_var_1` 与 `var_save_1` 同时安装：
  `保存选中行`（`saveVariable(0)`）、`保存本页全部修改`（新增 `saveBooleanPage()`）、`全量导出…`、`全量导入并保存…`。
- 新增 `void QtMainWindow::saveBooleanPage()`：读取表格当前全部行，收集与快照不同的布尔值，调用批量事务写入并刷新。
- `var_save_1` 设置 tooltip：`点击保存选中行；右键可保存本页全部修改、全量导出或全量导入。`
- 导出/导入状态文案对齐 Python：
  - 导出：`已全量导出 %d 项布尔变量，共 %d 字节；页面未保存修改不包含在内`
  - 导入：`已全量导入 %d 项布尔变量，实际修改 %d 项`
- 导入改为“读取快照 → 批量比较交换 → 回滚”，复用 `VariableService::writeBools`。

### C2 能力门控（`src/qt_mainwindow.cpp`）
- 新增 `void QtMainWindow::applyCapabilities()`，在附加成功与解绑时调用：
  - `checkBox_1..9`：能力键不可读写时禁用并设置 `profile.capabilities[key].reason` 为 tooltip；可写时恢复。
  - 变量页 `var_save_1..3` 与列表按 `variables_bool/int/ptr` 可读/可写门控。
  - 天赋/必杀/仓库/人物页保存按钮按对应能力门控。
- `toggleControl` 删除 `fixedControlPatches` 固定地址回退；仅使用 `ProfileRegistry::resolvePatches`，无配方时抛错并回滚复选框。
- 6.6 控制项：除 `control_auto_return` 外若画像无签名配方，复选框禁用并提示“当前基底没有已证据化的补丁配方”。

### C3 状态与文案
- 关键失败前缀对齐 Python：`读取天赋失败: `、`读取专属/套装失败: `、`保存天赋失败: `、`布尔全量导出失败: `、`布尔全量导入失败: `。
- `saveEffect` 失败时保留原 UI 值并刷新，不写入部分记录。

## 6. 契约覆盖与文档（D）

### D1 `docs/native_contract_map.csv`
列：`source,name,nativeStatus,evidence`。按源文件给默认值，个别覆盖。终态映射：

| Python 源 | nativeStatus |
|---|---|
| test_battle_tick_transactions.py / test_patch_runtime_memory.py | `covered:transaction_contracts` |
| test_boolean_profile.py / test_variable_storage.py | `covered:variable_contracts` |
| test_boolean_ui.py / test_ui.py / test_legacy_ui_compat.py / test_single_exe_ui.py / test_patch_lifecycle_ui.py | `covered:qt_ui_contracts` |
| test_build_release.py | `not-applicable:python-build-script` |
| test_capability_evidence.py / test_evidence_collector.py | `covered:evidence_contracts` |
| test_compatibility.py / test_engine_profile.py / test_legacy_runtime_profiles.py / test_runtime_catalog.py / test_runtime_regions.py / test_single_exe_profile.py | `covered:profile_contracts` |
| test_engine_layout.py / test_semantic_resolver.py | `not-applicable:python-internal-abstraction`（其边界/指针不变量由 `profile_contracts` 覆盖） |
| test_item_layout.py / test_legacy_split_consumables.py | `covered:item_contracts` |
| test_main_cli.py | `covered:cli_contracts` |
| test_process_memory.py | `covered:memory_contracts` |
| test_startup_smoke.py | `covered:startup_contracts` |
| test_verify_release.py / test_build_release.py 的 verify 部分 | `covered:verify_release_contracts` |
| test_windows_api_compatibility.py | `covered:platform_contracts` |

### D2 `tools/apply_contract_status.py`
读取 `resources/baseline.json` 与 `docs/native_contract_map.csv`，写入每条的 `nativeStatus`；未匹配到映射即报错，防止静默 `pending`。

### D3 `tests/parity_tests.cpp` + CMake
单一可执行 `parity_tests`，用子命令注册 CTest：
`memory_contracts`、`profile_contracts`、`item_contracts`、`variable_contracts`、`transaction_contracts`、`evidence_contracts`、`cli_contracts`、`startup_contracts`、`coverage_contracts`、`verify_release_contracts`、`platform_contracts`。
`coverage_contracts` 通过编译定义 `BANSHOU_SOURCE_DIR` 读取 baseline 与 map，断言：无 `pending`、每条终态合法、`covered:` 指向的 CTest 名在白名单内、N/A 理由码在白名单内、总数 399。

### D4 文档
更新 `docs/python_cpp_migration_matrix.csv` 每行 `status/verified_by`；更新 `README.md`、`ACCEPTANCE.md` 的迁移结论与发布模式说明。

## 7. 发布（E）

`tools/build_release.ps1`：

1. 单文件是默认发布形态：静态 Qt 由 `tools/build_static_qt.ps1` 从本机 Qt 源码
   编译（`-static -static-runtime`，x86），再用
   `BANSHOU_SINGLE_EXE=ON` 构建，产物 `dist/扳手.exe`，模式 `single-exe-static`；
   由 `tools/check_single_exe.ps1` 审计导入表，禁止 Qt/VC 运行库依赖。
2. 无静态 Qt 且暂不编译时，退化为 Qt 5.14.2 动态版构建，生成 `dist/portable/`：
   - `扳手.exe`
   - `Qt5Core.dll`、`Qt5Gui.dll`、`Qt5Widgets.dll`
   - `platforms/qwindows.dll`、`styles/qwindowsvistastyle.dll`
   - `vc_redist.x86.exe` 或 `msvcp140.dll/vcruntime140.dll`（若系统缺失）
   - `README-便携版.txt`（说明模式、启动方式、依赖）
   - 打包 `dist/扳手-C++版-portable-x86.zip`
   - 模式 `portable-dynamic-qt`
3. 写 `dist/release.json`：`mode`、`exeSha256`、`files[]`、`builtAt`。

`tools/verify_release.ps1` 增加：
- 读取 `release.json` 并校验模式与实际产物一致。
- EXE 版本资源、SHA256、`--startup-smoke-exit` 退出码。
- 真实样本 `--evidence-static` 并 `--evidence-inspect`。
- portable 模式校验 DLL/plugins/说明齐全，zip 可读。
- 任一失败退出非零。

## 8. 验证命令

```powershell
cmake -S . -B C:\banshou-build\cmake4d -G "Visual Studio 17 2022" -A Win32 -DCMAKE_PREFIX_PATH=D:/Qt/5.14.2/msvc2017
cmake --build C:\banshou-build\cmake4d --config Release --parallel 2
ctest --test-dir C:\banshou-build\cmake4d -C Release --output-on-failure
python tools/apply_contract_status.py
powershell -ExecutionPolicy Bypass -File tools/build_release.ps1
powershell -ExecutionPolicy Bypass -File tools/verify_release.ps1
```

真实样本：
- 6.5：`20260820_104551_加强版6.5未加密版\Ekd5.exe`
- 6.6：`Ccz66ItemIconSmoke_20260820_084836\Ekd5.exe`

## 9. 非目标与风险

- 不重写 Python 抽象类层次（`engine_layout.py`/`semantic_resolver.py`），只迁移其被 UI/画像使用的行为与不变量，理由见 D1。
- ~~静态 Qt 未安装时不做源码编译 Qt~~（2026-11-16 修正：本机仅 Qt 5.14.2 动态版，
  而单文件发布是硬需求，故改用源码编译静态 Qt；构建流程见 `tools/build_static_qt.ps1`，
  耗时一次性，之后仍以 `BANSHOU_SINGLE_EXE=ON` 出包）。
- 运行时断点采集需要真实 x86 进程；自动测试使用 `mock_target --breakpoint-spin`，真实游戏仅做人工验收记录。
- 不修改 `老版游戏制作工具/扳手` 的 Python 源码。
