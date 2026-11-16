"""Generate a reviewable Python-to-C++ migration matrix.

This is a development tool. The released executable does not import Python.
"""
import csv
import re
from pathlib import Path

project = Path(__file__).resolve().parents[1]
legacy = project.parents[1] / "老版游戏制作工具" / "扳手"
python_source = (legacy / "mywindow.py").read_text(encoding="utf-8-sig")
cpp_source = (project / "src" / "qt_mainwindow.cpp").read_text(encoding="utf-8-sig")
cpp_methods = set(re.findall(r"QtMainWindow::([A-Za-z_]\w*)\s*\(", cpp_source))

widgets = sorted(set(re.findall(r"self\.([A-Za-z_]\w+)\s*=", python_source)))
slots = sorted(set(re.findall(r"^\s*def\s+([A-Za-z_]\w*)\s*\(", python_source, re.M)))
pages = {
    "control": "toggleControl",
    "person": "loadPerson/savePerson/recalculatePerson",
    "battle": "loadBattle/saveBattle/reviveBattleUnit/runAutoRevive",
    "warehouse": "loadWarehouseRecord/saveWarehouse/warehouseBulk",
    "effects": "loadEffect/saveEffect",
    "variables": "refreshVariables/saveVariable",
    "diy": "refreshDiy/saveDiyValue",
    "fatal": "loadFatal/saveFatal",
}

rows = []
for page, methods in pages.items():
    for method in methods.split("/"):
        rows.append({
            "kind": "page",
            "python": method,
            "cpp": method,
            "status": "implemented" if method in cpp_methods else "missing",
            "page": page,
        })
for slot in slots:
    candidate = {
        "onData": "loadPerson",
        "saveData": "savePerson",
        "onWar": "loadBattle",
        "saveWar": "saveBattle",
        "onLife": "reviveBattleUnit",
        "onKill": "killBattleArea",
        "onLock": "addBattleLock",
        "offLock": "removeBattleLock",
        "LockThread": "runBattleLocks",
        "onVar": "refreshVariables",
        "saveVar": "saveVariable",
        "DIY_Page": "refreshDiy",
        "saveDIY": "saveDiyValue",
        "onMk": "loadFatal",
        "saveMk": "saveFatal",
    }.get(slot, "")
    if candidate:
        rows.append({
            "kind": "slot",
            "python": slot,
            "cpp": candidate,
            "status": "implemented" if candidate in cpp_methods else "missing",
            "page": "mapped",
        })

out_dir = project / "docs"
out_dir.mkdir(exist_ok=True)
out = out_dir / "python_cpp_migration_matrix.csv"
with out.open("w", encoding="utf-8-sig", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=["kind", "python", "cpp", "status", "page"])
    writer.writeheader()
    writer.writerows(rows)
print(f"widgets={len(widgets)} slots={len(slots)} rows={len(rows)} output={out}")
