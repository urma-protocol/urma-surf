#!/usr/bin/python3
"""GUI fixture adapter. Only explicit offline commands, with an append-only call log.

The fixture has real signed transactions; canned manifests are NOT proof that the
GUI itself or this mock verifies cryptography. store_gate exercises the real CLI.
"""
import json
import os
from pathlib import Path
import sys
import subprocess
import time


def fail(message):
    print(message, file=sys.stderr)
    raise SystemExit(7)


def option(arguments, key):
    return arguments[arguments.index(key) + 1]


def main():
    work = Path(os.environ["BROWSER_TEST_WORK"])
    args = sys.argv[1:]
    with (work / "calls.jsonl").open("a") as calls:
        calls.write(json.dumps({"args": args, "network": os.environ.get("URMA_NETWORK")}) + "\n")
    state = json.loads((work / "state.json").read_text())
    fixture = json.loads((work / "fixture.json").read_text())["publications"]
    if args[:2] == ["names", "resolve"]:
        network = option(args, "--network")
        registry = option(args, "--registry")
        if registry != Path(option(args, "--index")).stem:
            fail("explicit registry differs from fallback index")
        name = args[-1]
        key = f"{network}/{registry}/{name}"
        if key in state.get("raw_names", {}):
            root = state["raw_names"][key]
        elif key in state["names"]:
            root = fixture[state["names"][key]]["root"]
        else:
            fail(f"fixture name unavailable: {key}")
        print(json.dumps({"network": network, "registry": registry,
                          "resolution": {"Bound": {"target": {"Publication": root}}}}))
    elif args[:2] == ["web", "manifest"]:
        real_cli = os.environ.get("BROWSER_TEST_REAL_CLI")
        if real_cli:
            result = subprocess.run([real_cli, *args], cwd=work, env=dict(os.environ),
                                    capture_output=True, text=True, timeout=10)
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)
            raise SystemExit(result.returncode)
        network, root = option(args, "--network"), option(args, "--root")
        item = next((p for p in fixture.values() if p["root"] == root), None)
        if item is None:
            fail("fixture root unavailable")
        files = [{k: f[k] for k in ("path", "mime", "sha256", "length")} for f in item["files"]]
        print(json.dumps({"format": "URMA-WEB-STORE-2", "network": network, "root": root,
                          "author": item["author"], "label": "GUI fixture", "entry": "index.html",
                          "tip_height": 1, "tip_hash": "11" * 32, "files": files, "pinned": []}))
    elif args[:2] == ["web", "fetch"]:
        root = option(args, "--root")
        behavior = state.get("fetch", {}).get(root, "refuse")
        if behavior == "wait":
            # Explicit file rendezvous, with its own bounded lifetime.
            (work / f"fetch-{root}.started").write_text("started")
            deadline = time.monotonic() + 15
            while not (work / f"fetch-{root}.release").exists():
                if time.monotonic() >= deadline:
                    fail("fixture fetch deadline")
                time.sleep(.05)
        (work / f"fetch-{root}.finished").write_text("refused")
        fail(f"fixture fetch refused [{state.get('fetch_epoch', 0)}]; no fallback or external access")
    else:
        fail(f"unexpected CLI command: {args}")


if __name__ == "__main__":
    main()
