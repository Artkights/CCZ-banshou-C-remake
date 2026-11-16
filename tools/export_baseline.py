"""Development-only oracle export. Native builds consume the checked-in JSON.

Schema 2 is the migration contract between the Python reference and the C++
implementation.  It intentionally exports complete legacy profiles (addresses,
limits, capabilities, patches, remote calls, metadata and table layouts) instead
of only the handful of fields the old native prototype happened to read.
"""
import ast
import dataclasses
import enum
import hashlib
import json
import sys
from pathlib import Path

project = Path(__file__).resolve().parents[1]
workspace = project.parents[1]
legacy = workspace / "老版游戏制作工具" / "扳手"
sys.path.insert(0, str(legacy))
import engine_profile as ep


def serial(value):
    if isinstance(value, enum.Enum):
        return value.value
    if dataclasses.is_dataclass(value):
        return {f.name: serial(getattr(value, f.name)) for f in dataclasses.fields(value)}
    if isinstance(value, bytes):
        return {"hex": value.hex()}
    if isinstance(value, dict):
        return {str(k): serial(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [serial(v) for v in value]
    if isinstance(value, (set, frozenset)):
        return [serial(v) for v in sorted(value, key=repr)]
    return value


def local_dict(function, name):
    tree = ast.parse((legacy / "engine_profile.py").read_text(encoding="utf-8-sig"))
    node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == function)
    for statement in node.body:
        if isinstance(statement, ast.Assign) and any(
                isinstance(t, ast.Name) and t.id == name for t in statement.targets):
            environment = dict(vars(ep))
            # ``structural`` reads a local alias that is not exported by the
            # module.  The alias always refers to the standard ABI pattern.
            environment.setdefault(
                "recalculate_pattern", ep.RECALCULATE_ABI_PATTERN_66)
            return eval(compile(ast.Expression(statement.value), "oracle-data", "eval"),
                        environment)
    raise ValueError((function, name))


def function_patterns(name):
    """Read a literal ``patterns`` mapping from one locator function."""
    return local_dict(name, "patterns")


constants = {
    name: serial(getattr(ep, name))
    for name in (
        "ITEM_ROW_SIZE_66", "ITEM_LOW_OFFSET_66", "ITEM_LOW_COUNT_66",
        "ITEM_HIGH_OFFSET_66", "ITEM_HIGH_COUNT_66", "ITEM_RECORD_COUNT_66",
        "ITEM_SPLIT_HIGH_COUNT", "ITEM_SPLIT_SLOT_COUNT",
        "ITEM_EXTENSION_OFFSET_66", "ITEM_EXTENSION_COUNT_66",
        "ITEM_EXTENSION_BYTES_66", "ITEM_STAR_CAPACITY_64_65",
        "ITEM_STAR_CAPACITY_66", "ITEM_MAX_RECORD_COUNT_66",
        "ITEM_BYTE_SENTINEL", "ITEM_BYTE_STORAGE_COUNT",
        "ITEM_EXTENSION_SENTINEL_NAMES", "EQUIPMENT_COUNT_66",
        "CONSUMABLE_ID_MIN_66", "CONSUMABLE_ID_MAX_66",
        "CONSUMABLE_STORAGE_COUNT_66", "CONSUMABLE_ID_MIN_LEGACY_EXPANDED",
        "CONSUMABLE_ID_MAX_LEGACY_EXPANDED",
        "CONSUMABLE_STORAGE_COUNT_LEGACY_EXPANDED",
        "CONSUMABLE_ID_MIN_SINGLE_EXE_66",
        "CONSUMABLE_STORAGE_COUNT_SINGLE_EXE_66",
        "EFFECT_ROW_SIZE_66", "EFFECT_CHARACTER_SLOTS_66",
        "EFFECT_JOB_SLOTS_66", "EFFECT_CHARACTER_COUNT_66",
        "EFFECT_EMPTY_CHARACTER_66", "EFFECT_JOB_COUNT_66",
        "EFFECT_NAME_RECORD_COUNT_65_66", "EFFECT_NAME_SLOT_SIZE_65_66",
        "EFFECT_NAME_RUNTIME_ADDRESS_65_66", "ITEM_CODEC_65",
        "ITEM_CODEC_66", "ITEM_CODEC_LEGACY", "BOOL_USER_COUNT_66",
        "BOOL_BROWSE_COUNT_66", "BOOL_USER_BYTES_66", "BOOL_BROWSE_BYTES_66",
        "BOOL_DOCUMENTED_COUNT_LEGACY", "REFERENCE_IMAGE_BASE",
    ) if hasattr(ep, name)
}

base66_item_layout = {
    "lowFile": "Data.e5", "lowOffset": ep.ITEM_LOW_OFFSET_66,
    "lowCount": ep.ITEM_LOW_COUNT_66, "highFile": "Star.e5",
    "highOffset": ep.ITEM_HIGH_OFFSET_66,
    "highCount": ep.ITEM_SPLIT_HIGH_COUNT, "rowSize": ep.ITEM_ROW_SIZE_66,
    "codec": ep.ITEM_CODEC_66, "nameSize": 0x0F,
    "extensionFile": "Star.e5", "extensionOffset": ep.ITEM_EXTENSION_OFFSET_66,
    "extensionCount": ep.ITEM_EXTENSION_COUNT_66, "extensionActive": False,
    "recordCount": ep.ITEM_SPLIT_SLOT_COUNT,
    "validCount": ep.ITEM_RECORD_COUNT_66,
    "sentinelId": ep.ITEM_BYTE_SENTINEL, "starFileLength": 0,
    "starReadCapacity": ep.ITEM_STAR_CAPACITY_66, "reason": "",
}

base66_variable_layouts = {
    "variables_bool": {
        "addressKey": "bool_vars", "countOffset": None, "dataOffset": 0,
        "stride": 1, "width": 1, "signed": False, "storage": "bitset",
        "bitOrder": "msb0", "storageBytes": ep.BOOL_USER_BYTES_66,
        "browseStorageBytes": ep.BOOL_BROWSE_BYTES_66,
        "browseRecordCount": ep.BOOL_BROWSE_COUNT_66,
        "writableRecordCount": 0, "formatVerified": False,
        "domainBasis": ep.BOOL_DOMAIN_BASIS_66,
        "nativeIndexBoundsVerified": False,
        "recordCount": ep.BOOL_USER_COUNT_66,
    },
    "variables_int": {
        "addressKey": "int_vars", "countOffset": None, "dataOffset": 0,
        "stride": 4, "width": 4, "signed": True, "recordCount": 4096,
    },
    "variables_ptr": {
        "addressKey": "ptr_vars", "countOffset": None, "dataOffset": 0,
        "stride": 4, "width": 4, "signed": False, "codec": "pointer-u32-v1",
        "recordCount": 481,
    },
}

base66_metadata = {
    "item_layout": base66_item_layout,
    "item_record_ids": list(range(ep.ITEM_RECORD_COUNT_66)),
    "equipment_item_ids": list(range(ep.EQUIPMENT_COUNT_66)),
    "item_row_codec": ep.ITEM_CODEC_66,
    "consumable_item_ids": list(
        range(ep.CONSUMABLE_ID_MIN_66, ep.CONSUMABLE_ID_MAX_66 + 1)),
    "consumable_slot_ids": list(
        range(ep.CONSUMABLE_ID_MIN_66, ep.CONSUMABLE_ID_MAX_66 + 1)),
    "consumable_valid_ids": [],
    "consumable_counts_source": "unavailable",
    "effect_empty_character": ep.EFFECT_EMPTY_CHARACTER_66,
    "effect_empty_characters": [ep.EFFECT_EMPTY_CHARACTER_66] * ep.EFFECT_CHARACTER_SLOTS_66,
    "auto_return_layout": {},
    "variable_layouts": base66_variable_layouts,
}

catalog = {
    "schema": 2,
    "generatedBy": "tools/export_baseline.py",
    "constants": constants,
    "legacy": {str(v): serial(ep.legacy_profile(v)) for v in range(6)},
    "base66": {
        "addresses": local_dict("detect_engine", "addresses"),
        "limits": local_dict("detect_engine", "limits"),
        "structural": local_dict("detect_engine", "structural"),
        "metadata": base66_metadata,
    },
    "consumables66": {"patterns": function_patterns("locate_consumable_mapping_66")},
    "consumablesLegacy": {
        "patterns": function_patterns("locate_consumable_mapping_legacy_expanded")},
    "consumablesSingleExe": {
        "patterns": function_patterns("locate_consumable_mapping_single_exe_66")},
    "consumablesSplit43": serial(ep.legacy_split_consumable_mapping()),
    "patchLocators": serial(ep._PATCH_LOCATORS),
    "patterns": {k: v for k, v in vars(ep).items()
                 if ("PATTERN" in k and isinstance(v, str))},
    "known66": serial(ep._KNOWN_66_IDENTITIES),
    "knownLegacy": serial(getattr(ep, "KNOWN_LEGACY_TARGETS", {})),
    "remoteCalls": {
        str(v): serial(ep.legacy_profile(v).remote_calls) for v in range(6)},
}

sources = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
           for p in sorted(legacy.glob("*.py"))}
sources.update({"tests/" + p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted((legacy / "tests").glob("*.py"))})
tests = []
for path in sorted((legacy / "tests").glob("test_*.py")):
    for node in ast.walk(ast.parse(path.read_text(encoding="utf-8-sig"))):
        if isinstance(node, ast.FunctionDef) and node.name.startswith("test_"):
            tests.append({"source": path.name, "name": node.name,
                          "nativeStatus": "pending"})
out = project / "resources"
out.mkdir(parents=True, exist_ok=True)
(out / "catalog.json").write_text(
    json.dumps(catalog, ensure_ascii=False, indent=2), encoding="utf-8")
(out / "baseline.json").write_text(
    json.dumps({"schema": 2, "sources": sources, "tests": tests},
               ensure_ascii=False, indent=2), encoding="utf-8")
print("Exported schema 2:", len(catalog["constants"]), "constants,",
      len(tests), "test contracts and", len(catalog["patterns"]), "patterns")
