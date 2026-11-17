"""Generate the Win32 control descriptor table from the Qt Designer .ui file.

Run once during development:

    python tools/gen_control_table.py

The generated src/win32_control_table.inc is checked in; the runtime never needs
Qt, Python, or the .ui file.  Geometry, text, font size and page membership are
copied verbatim from the .ui so the native window is a strict reproduction.
"""
from __future__ import annotations

import html
import re
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "design" / "mainwindow.ui"
OUT = ROOT / "src" / "win32_control_table.inc"

CONTAINER_CLASSES = {"QMainWindow", "QWidget", "QFrame", "QToolBar"}

KIND = {
    "QLabel": "CTL_LABEL",
    "QTextEdit": "CTL_EDIT",
    "QLineEdit": "CTL_EDIT",
    "QComboBox": "CTL_COMBO",
    "QPushButton": "CTL_BUTTON",
    "QCheckBox": "CTL_CHECK",
    "QRadioButton": "CTL_RADIO",
    "QListWidget": "CTL_LIST",
    "QTableWidget": "CTL_TABLE",
}

PAGES = ["控制", "人物", "战场", "仓库", "天赋", "变量", "自定", "必杀"]
PAGE_ACTIONS = ["action_C", "action_M", "action_B", "action_W",
                "action_T", "action_V", "action_D", "action_K"]

# The 7 controls that QtMainWindow created at runtime (not present in the .ui).
DYNAMIC = [
    ("war_input_51", "CTL_EDIT", 2, 185, 170, 25, 25, "", 9, 0, False),
    ("war_input_52", "CTL_EDIT", 2, 255, 170, 25, 25, "", 9, 0, False),
    ("war_input_53", "CTL_EDIT", 2, 185, 200, 25, 25, "", 9, 0, False),
    ("war_input_54", "CTL_EDIT", 2, 255, 200, 25, 25, "", 9, 0, False),
    ("var_save_1", "CTL_BUTTON", 5, 170, 20, 50, 25, "保存", 9, 0, True),
    ("var_save_2", "CTL_BUTTON", 5, 390, 20, 50, 25, "保存", 9, 0, True),
    ("var_save_3", "CTL_BUTTON", 5, 610, 20, 50, 25, "保存", 9, 0, True),
]


def property_value(widget, name):
    for prop in widget.findall("./property"):
        if prop.get("name") == name:
            return prop
    return None


def geometry(widget):
    prop = property_value(widget, "geometry")
    rect = prop.find("./rect") if prop is not None else None
    if rect is None:
        return None
    values = {}
    for tag in ("x", "y", "width", "height"):
        node = rect.find("./" + tag)
        if node is None or node.text is None:
            return None
        values[tag] = int(node.text)
    return (values["x"], values["y"], values["width"], values["height"])


def plain_text(widget):
    prop = property_value(widget, "text")
    if prop is None:
        return ""
    node = prop.find("./string")
    if node is None or not node.text:
        return ""
    text = html.unescape(node.text)
    if "<html" in text:
        text = re.sub(r"<[^>]+>", "", text)
        text = html.unescape(text).strip()
    return text


def point_size(widget):
    prop = property_value(widget, "font")
    if prop is None:
        return 9
    node = prop.find("./font/pointsize")
    if node is None or not node.text:
        return 9
    return int(float(node.text))


def alignment(widget):
    align = property_value(widget, "alignment")
    if align is not None:
        raw = " ".join(node.text or "" for node in align.iter())
        if "AlignCenter" in raw:
            return 2
        if "AlignRight" in raw:
            return 1
    prop = property_value(widget, "text")
    if prop is None:
        return 0
    node = prop.find("./string")
    if node is None or not node.text:
        return 0
    text = html.unescape(node.text)
    if 'align="right"' in text or "align=&quot;right&quot;" in text:
        return 1
    if 'align="center"' in text:
        return 2
    return 0


def c_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return 'L"' + escaped + '"'


def main():
    root = ET.parse(UI).getroot()
    def page_of(widget, parent_map):
        current = widget
        while current is not None:
            match = re.fullmatch(r"widget_([0-8])", current.get("name", ""))
            if match:
                number = int(match.group(1))
                return 8 if number == 0 else number - 1
            current = parent_map.get(current)
        return 255

    records = []
    parent_map = {child: parent for parent in root.iter() for child in parent}
    for widget in root.iter("widget"):
        cls = widget.get("class", "")
        if cls in CONTAINER_CLASSES:
            # widget_0..widget_8 are real page panels in the Designer file;
            # create them as HWNDs so a whole page can be shown/hidden at once.
            if not re.fullmatch(r"widget_[0-8]", widget.get("name", "")):
                continue
        rect = geometry(widget)
        if rect is None:
            raise SystemExit("missing geometry: " + widget.get("name", "?"))
        records.append({
            "name": widget.get("name"),
            "kind": "CTL_PAGE" if cls in CONTAINER_CLASSES else KIND.get(cls),
            "page": page_of(widget, parent_map),
            "rect": rect,
            "text": plain_text(widget),
            "pointSize": point_size(widget),
            "align": alignment(widget),
            "visible": True,
        })

    for name, kind, page, x, y, w, h, text, size, align, visible in DYNAMIC:
        records.append({
            "name": name, "kind": kind, "page": page,
            "rect": (x, y, w, h), "text": text,
            "pointSize": size, "align": align, "visible": visible,
        })

    if len(records) != 315:
        raise SystemExit(f"expected 315 controls, got {len(records)}")

    lines = [
        "// Generated by tools/gen_control_table.py from ui/mainwindow.ui.",
        "// Do not edit by hand.  The native window reproduces these values exactly.",
        "#pragma once",
        "#include <cstddef>",
        "",
        "namespace banshou {",
        "enum WinCtlKind { CTL_LABEL, CTL_EDIT, CTL_COMBO, CTL_BUTTON, CTL_CHECK,",
        "                  CTL_RADIO, CTL_LIST, CTL_TABLE, CTL_TOOLBTN, CTL_PICTURE, CTL_PAGE };",
        "",
        "struct WinControlDesc {",
        "  const wchar_t *name;",
        "  int id;",
        "  int kind;",
        "  int page;",
        "  int x, y, w, h;",
        "  const wchar_t *text;",
        "  int pointSize;",
        "  int align;",
        "  bool visible;",
        "};",
        "",
        "static const WinControlDesc kControls[] = {",
    ]

    for index, record in enumerate(records):
        if record["kind"] is None:
            raise SystemExit("unmapped class for " + record["name"])
        x, y, w, h = record["rect"]
        lines.append(
            "    {%s, %d, %s, %d, %d, %d, %d, %d, %s, %d, %d, %s}," % (
                c_string(record["name"]), 1000 + index, record["kind"],
                record["page"], x, y, w, h, c_string(record["text"]),
                record["pointSize"], record["align"],
                "true" if record["visible"] else "false"))

    lines += [
        "};",
        "static const size_t kWinControlCount = %d;" % len(records),
        "",
        "// Toolbar buttons mirror the 8 QActions in the .ui file.",
        "struct WinToolDesc { int id; int page; const wchar_t *text; };",
        "static const WinToolDesc kWinTools[] = {",
    ]
    for index, action in enumerate(PAGE_ACTIONS):
        lines.append("    {9000 + %d, %d, %s}," % (
            index, index, c_string(PAGES[index] + "(" + action[-1] + ")")))
    lines += [
        "};",
        "static const size_t kWinToolCount = 8;",
        "",
        "}  // namespace banshou",
        "",
    ]

    OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"generated {OUT.relative_to(ROOT)}: {len(records)} controls, 8 tools")


if __name__ == "__main__":
    main()
