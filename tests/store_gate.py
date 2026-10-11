#!/usr/bin/env python3
"""Real CLI authentic offline store gate; no node, fetch, names scan or browser.

CLI --core is provenance, not a build request. Pass a binary built from that
checkout explicitly. Store metadata describes an observation; no inclusion,
funding consensus or live public-source verification is claimed here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

from fixture_store import load_fixture, materialize


def signature_offset(transaction):
    offset = 6  # version and marker/flag
    assert transaction[4:6] == b"\x00\x01"

    def compact():
        nonlocal offset
        first = transaction[offset]
        offset += 1
        size = {253: 2, 254: 4, 255: 8}.get(first, 0)
        if size:
            value = int.from_bytes(transaction[offset:offset + size], "little")
            offset += size
            return value
        return first

    assert compact() == 1
    offset += 36
    size = compact()
    offset += size
    offset += 4
    for _ in range(compact()):
        offset += 8
        size = compact()
        offset += size
    assert compact() == 3 and compact() == 64
    return offset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--urma", type=Path, required=True)
    parser.add_argument("--core", type=Path, help="optional Git checkout provenance; not needed in source tarball")
    args = parser.parse_args()
    binary = args.urma.resolve(strict=True)
    commit = subprocess.check_output(["git", "-C", str(args.core), "rev-parse", "HEAD"],
                                     text=True, timeout=5).strip() if args.core else "operator-supplied binary"
    fixture = load_fixture()
    with tempfile.TemporaryDirectory(prefix="urma-browser-store-") as directory:
        work = Path(directory)
        store = work / "store"
        materialize(store, fixture)
        env = {"PATH": os.defpath, "HOME": str(work), "XDG_STATE_HOME": str(work / "state"),
               "XDG_DATA_HOME": str(work / "data"), "XDG_CONFIG_HOME": str(work / "config"),
               "URMA_CONFIG": str(work / "missing-config"), "URMA_LOG_OUTPUT": "stderr",
               "URMA_OUTPUT": "json", "URMA_NETWORK": "litecoin-mainnet",
               "URMA_RPC_URL": "http://127.0.0.1:1/", "URMA_NODE_AUTH_FILE": str(work / "missing-cookie")}

        def manifest(root, expected_error=None):
            result = subprocess.run([str(binary), "web", "manifest", "--store", str(store),
                                     "--network", "litecoin-mainnet", "--root", root],
                                    cwd=work, env=env, capture_output=True, text=True, timeout=10)
            if expected_error is None:
                assert result.returncode == 0, (result.returncode, result.stderr)
                return json.loads(result.stdout)
            assert result.returncode == 1, (result.returncode, result.stderr)
            assert expected_error in result.stderr, result.stderr

        passed = 0
        for item in fixture["publications"].values():
            summary = manifest(item["root"])
            assert summary["format"] == "URMA-WEB-STORE-2"
            assert summary["entry"] == "index.html"
            assert summary["root"] == item["root"] and summary["author"] == item["author"]
            assert summary["payload_sha256"] == item["payload_sha256"]
            assert summary["files"] == [{k: f[k] for k in ("path", "mime", "length", "sha256")}
                                        for f in item["files"]]
            passed += 1
        item = fixture["publications"]["A"]
        root = item["root"]
        transaction = store / "tx" / f"{root}.bin"
        original = transaction.read_bytes()
        signature_start = signature_offset(original)
        poisoned = bytearray(original)
        poisoned[signature_start] ^= 1
        transaction.write_bytes(poisoned)
        manifest(root, "invalid author signature")
        assert transaction.read_bytes() == poisoned
        transaction.write_bytes(original)
        passed += 1
        payload = store / "objects" / item["payload_sha256"]
        original_payload = payload.read_bytes()
        payload.write_bytes(original_payload + b"corrupt")
        manifest(root, "failed re-verification")
        assert payload.read_bytes() == original_payload + b"corrupt"
        payload.write_bytes(original_payload)
        passed += 1
        pointer = store / "publications/litecoin-mainnet" / f"{root}.json"
        original_pointer = pointer.read_bytes()
        changed = json.loads(original_pointer)
        changed["network"] = "litecoin-testnet"
        pointer.write_text(json.dumps(changed))
        manifest(root, "stored pointer names another network or root")
        pointer.write_bytes(original_pointer)
        passed += 1
        manifest(root)  # restored input still passes without any migration or scan
        passed += 1
        print(json.dumps({"gate": "authentic-offline-store", "passed": passed,
                          "fixture_sha256": hashlib.sha256(Path(__file__).with_name("fixtures").joinpath("authentic.json").read_bytes()).hexdigest(),
                          "core_commit": commit, "cli_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                          "limitation": "binary provenance supplied by operator; no inclusion or complete child-graph verification claim"}))


if __name__ == "__main__":
    main()
