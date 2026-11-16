"""Apply native contract statuses to resources/baseline.json.

The source-level map in docs/native_contract_map.csv must cover every Python
test.  An unmatched contract is an error: silently leaving ``pending`` would
make the migration report look complete when it is not.
"""

from __future__ import annotations

import csv
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASELINE = ROOT / "resources" / "baseline.json"
MAP = ROOT / "docs" / "native_contract_map.csv"

COVERED = re.compile(r"^covered:[A-Za-z0-9_]+$")
NOT_APPLICABLE = re.compile(r"^not-applicable:[a-z0-9-]+$")


def main() -> int:
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    if baseline.get("schema") != 2:
        print("baseline schema must be 2", file=sys.stderr)
        return 1
    defaults = {}
    exact = {}
    with MAP.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            source = (row.get("source") or "").strip()
            name = (row.get("name") or "").strip()
            status = (row.get("nativeStatus") or "").strip()
            if not source or not (
                    status == "pending" or COVERED.match(status) or
                    NOT_APPLICABLE.match(status)):
                print("invalid contract map row: %r" % row, file=sys.stderr)
                return 1
            if name == "*":
                defaults[source] = status
            else:
                exact[(source, name)] = status
    tests = baseline.get("tests")
    if not isinstance(tests, list) or not tests:
        print("baseline tests missing", file=sys.stderr)
        return 1
    seen = set()
    for test in tests:
        source = str(test.get("source", ""))
        name = str(test.get("name", ""))
        status = exact.get((source, name), defaults.get(source))
        if not status or status == "pending":
            print("unmapped contract: %s::%s" % (source, name), file=sys.stderr)
            return 1
        test["nativeStatus"] = status
        seen.add(source)
    missing = sorted(set(defaults) - seen)
    if missing:
        print("map sources not present in baseline: " + ", ".join(missing),
              file=sys.stderr)
        return 1
    BASELINE.write_text(
        json.dumps(baseline, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    counts = {}
    for test in tests:
        counts[test["nativeStatus"]] = counts.get(test["nativeStatus"], 0) + 1
    print("applied %d contracts: %s" % (len(tests), json.dumps(
        counts, ensure_ascii=False, sort_keys=True)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
