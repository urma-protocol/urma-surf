#!/usr/bin/env python3
"""Bounded, standalone ASan/UBSan parser/tail/initiator/metadata gate; no browser or CLI."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    source = args.source.resolve()
    flags = shlex.split(subprocess.check_output([
        "pkg-config", "--cflags", "--libs", "gtk+-3.0", "webkit2gtk-4.1",
        "json-glib-1.0", "libsoup-3.0",
    ], text=True, timeout=10))
    cases = json.loads((source / "tests/fixtures/bridge.json").read_text())
    legacy = {"format": "URMA-WEB-STORE-2", "tip_height": 7, "tip_hash": "ab" * 32}
    operator = {"format": "URMA-WEB-STORE-3", "source": {"kind": "local-node-operator"}}
    metadata = [(legacy, True, False), (operator, True, True)]
    for value in [dict(operator, source={"kind": "unknown"}),
                  dict(operator, source={"kind": "local-node-operator", "extra": 1}),
                  dict(operator, tip_height=0), dict(operator, tip_hash=None),
                  {"format": "URMA-WEB-STORE-3"}, dict(operator, source=None),
                  dict(legacy, source=operator["source"]), dict(legacy, tip_height=None),
                  dict(legacy, tip_hash=4), dict(legacy, tip_height=-1),
                  dict(legacy, format="unknown"), {"format": 3}]:
        metadata.append((value, False, False))
    for value, accepted, is_operator in metadata:
        case = {"args": ["metadata", json.dumps(value)], "exit": 0 if accepted else 1}
        if accepted:
            case["stdout"] = f"accepted=1 operator={int(is_operator)}"
        cases.append(case)
    env = {"PATH": os.defpath, "LANG": "C", "URMA_BIN": "/nonexistent/urma",
           "ASAN_OPTIONS": "detect_leaks=0:abort_on_error=1",
           "UBSAN_OPTIONS": "halt_on_error=1:print_stacktrace=1"}
    with tempfile.TemporaryDirectory(prefix="urma-surf-bridge-") as temporary:
        binary = Path(temporary) / "bridge_test"
        subprocess.run([
            "cc", "-g", "-O1", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=undefined", "-o", str(binary),
            str(source / "tests/bridge_test.c"), *flags,
        ], check=True, timeout=45)
        for case in cases:
            result = subprocess.run([str(binary), *case["args"]], cwd=temporary,
                                    env=env, capture_output=True, text=True, timeout=5)
            assert result.returncode == case["exit"], (case, result.returncode, result.stderr)
            assert "AddressSanitizer" not in result.stderr, (case, result.stderr)
            assert "runtime error:" not in result.stderr, (case, result.stderr)
            if "stdout" in case:
                assert result.stdout.strip() == case["stdout"], (case, result.stdout)
        print(json.dumps({"gate": "asan-bridge", "passed": len(cases), "failed": 0,
                          "coverage": "parse/tail/initiator/metadata; resolver, store and GUI not executed"}))


if __name__ == "__main__":
    main()
