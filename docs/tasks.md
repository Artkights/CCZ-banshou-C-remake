# C++ 迁移收尾任务清单

对应规格：[spec.md](spec.md)

## T1 证据采集器底层
- [x] `include/image.hpp`、`src/image.cpp`：新增 `PEImage::findLiteralReferences`。
- [x] `include/profile.hpp`、`src/profile.cpp`：`Capability` 增加 `state/reasonCode/recipe`，解析 `capabilities` 与 `remoteCalls`。
- [x] `src/evidence.cpp`：实现 canonical JSON、摘要、recipeId、运行时检查表、bundle 构造/校验、EvidenceRepository（写入/清单/加载/verifiedRecipe）。
- [x] 验证：`native_contracts` 保持绿；新增证据单测覆盖摘要篡改拒绝、清单往返。

## T2 反汇编与静态/ORACLE 采集
- [x] `src/evidence.cpp`：capstone 反汇编、规范化指纹、`reconstructLegacyAutoReturnStub`。
- [x] `collectStatic`：能力遍历、签名/操作数地址、风险窗口、字面引用计数。
- [x] `collectLegacyOracle`：stub 原始/重构/差异偏移。
- [x] `src/main.cpp`：注册 `--evidence-static/--evidence-oracle`。
- [x] 验证：6.5/6.6 真实样本生成 bundle，`--evidence-inspect` 返回 0。

## T3 运行时断点采集
- [x] `src/evidence.cpp`：Toolhelp32 模块定位、DebugActiveProcess、0xCC 断点、CONTEXT 采样、watch、单步恢复、超时/追加/标签。
- [x] `tests/mock_target.cpp`：新增 `--breakpoint-spin`。
- [x] `src/main.cpp`：注册 `--evidence-runtime`。
- [x] 验证：CI 内对 `mock_target --breakpoint-spin` 采集 ≥1 样本；失败路径返回非零。

## T4 inspect/verify/manifest
- [x] `src/evidence.cpp`：谓词求值器、`verifyCapturedBundle`、`inspectBundle`、`rebuildManifest`。
- [x] `src/main.cpp`：注册 `--evidence-inspect/--evidence-verify/--evidence-manifest`。
- [x] 验证：篡改 bundle 被拒绝；合同未通过返回 2；manifest 重建后加载一致。

## T5 事务写入
- [x] `src/game.cpp`：Effect/Exclusive/Fatal/Person/Warehouse/Battle/BattleState 改为快照 + 比较交换 + 回滚；Exclusive 保留未知字节。
- [x] `include/game.hpp`：新增 `VariableService::writeBools` 等必要声明。
- [x] 验证：`transaction_contracts` 覆盖 stale 拒绝、第二条目失败回滚、未知字节保留。

## T6 UI 对齐
- [x] `src/win32_mainwindow.cpp`：四项布尔右键动作、`saveBooleanPage`、tooltip、状态文案。
- [x] `src/win32_mainwindow.cpp`：`applyCapabilities` 门控；删除控制补丁固定地址回退；6.6 无配方控制项禁用。
- [x] 验证：`win32_ui_contracts` 断言两个控件各 4 个 action、文案与 tooltip；`cli_contracts` 验证只读会话拒绝写。

## T7 契约映射与覆盖测试
- [x] `docs/native_contract_map.csv`：按规格 §6 生成全量映射。
- [x] `tools/apply_contract_status.py`：写回 `resources/baseline.json`，未映射即失败。
- [x] `tests/parity_tests.cpp`：memory/profile/item/variable/transaction/evidence/cli/startup/coverage/verify_release/platform 子命令。
- [x] `CMakeLists.txt`：`parity_tests` 与 11 个 CTest 注册；`coverage_contracts` 编译定义 `BANSHOU_SOURCE_DIR`。
- [x] 验证：`ctest` 全绿；`coverage_contracts` 断言 399 条无 pending。

## T8 UI 合同扩展
- [x] `tests/qt_ui_tests.cpp`：布尔四项动作、动态控件几何、能力门控初始状态、八页动作。
- [x] 验证：`win32_ui_contracts` 全绿。

## T9 发布脚本
- [x] `tools/build_release.ps1`：静态 Win32 探测 / 便携动态打包 / `release.json` / zip。
- [x] `tools/verify_release.ps1`：模式、SHA、启动自检、证据、文件清单。
- [x] `README.md`、`ACCEPTANCE.md`：迁移结论与发布模式。
- [x] 验证：脚本全绿，输出最终 EXE SHA256 与 zip 路径。

## T10 全量回归与结论
- [x] 运行全部 CTest、`verify_release.ps1`、6.5/6.6 样本证据。
- [x] 更新 `docs/python_cpp_migration_matrix.csv` 状态与证据列。
- [x] 输出迁移完成结论：契约 399 条终态清单、CTest 结果、发布产物摘要。

## T11 单文件静态版（含运行库）
- [x] `tools/（已移除的静态 Qt 构建脚本）`：MSVC v142 + x86 + `-static-runtime` 编译 Win32 原生，
      手工装配 INCLUDE/LIB/PATH（不依赖 `VsDevCmd` 的 `reg.exe`），`-VerifyOnly` 预检。
- [x] `CMakeLists.txt`：单文件模式的 Win32 版本改为 `BANSHOU_QT_MIN_VERSION`（默认 5.14）。
- [x] `tools/build_release.ps1`：`Invoke-Native` 隔离原生命令 stderr；
      工具集探测改用 `[IO.Directory]` 并支持 `BANSHOU_MSVC_TOOLSET`。
- [x] `tools/check_single_exe.ps1`：修复 dumpbin 分支逻辑与 `/imports` 正则，
      改用 `/dependents`；补齐 `2022\Community` 层级探测。
- [x] 产物：`dist/扳手.exe` 13,933,568 字节，SHA256 `8C52A65B…CCCCA`，
      仅 import 系统 DLL。
- [x] 验证：CTest 15/15、`verify_release.ps1` success、空目录独立启动八页通过。
