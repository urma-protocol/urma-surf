#!/usr/bin/env python3
"""Generate public test-only signed fixtures; --core is read only, no SDK/node.

Independent Python writers are imported without invoking their main functions.
Their output is rebound to our temporary directory before generating proofs.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    commit = subprocess.check_output(["git", "-C", str(args.core), "rev-parse", "HEAD"], text=True, timeout=5).strip()

    def module(name, filename):
        spec = importlib.util.spec_from_file_location(name, args.core / "scripts" / filename)
        value = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(value)
        return value

    v = module("browser_proof_primitives", "generate-vectors.py")
    web = module("browser_package_writer", "generate-web-vectors.py")
    fixtures = {}
    with tempfile.TemporaryDirectory(prefix="urma-surf-fixture-generator-") as directory:
        v.OUT = Path(directory)
        v.manifest = {"proofs": []}

        def proof(name, record):
            v.proof(name, record)
            item = v.manifest["proofs"][-1]
            descriptor = {"txid": item["txid"], "commit_hex": (v.OUT / item["commit"]["file"]).read_bytes().hex(),
                          "reveal_hex": (v.OUT / item["reveal"]["file"]).read_bytes().hex(), "record_hex": record.hex()}
            return item, descriptor

        for label in "ABCD":
            html = ("<!doctype html><html><head><meta charset=utf-8><title>Fixture " + label + "</title>"
                    "<style>body{margin:0;font:20px monospace}a{display:block;height:50px}</style></head><body>"
                    '<a href="urma://beta.ltc/" target="_blank">child beta</a>'
                    '<a href="urma://alpha.ltc/">alpha</a><a href="urma://beta.ltc/">beta</a>'
                    '<button onclick="window.open(\'urma://beta.ltc/\',\'_blank\')">script child</button>'
                    '<a href="sub.html">relative</a><a href="query.html">query</a><a href="frame.html">frames</a><a href="#section">fragment</a>'
                    '<a href="urma://' + "f" * 64 + '.ltc/">failed publication</a>'
                    '<a href="save.bin">save</a><p id=section>fragment destination</p></body></html>').encode()
            query = ("<title>Query " + label + "</title><script>history.pushState({},'', '?step=1')</script>"
                     "<button onclick=\"history.pushState({},'', '?step=2')\">query after load</button>").encode()
            frame = ("<title>Frames " + label + "</title><p id=result>ready</p><iframe id=child></iframe>"
                     "<button onclick=\"document.getElementById('result').textContent='waiting';"
                     "let f=document.getElementById('child');f.onload=()=>{document.getElementById('result').textContent='child completed'};"
                     "f.src='undeclared-frame.html';fetch('undeclared-frame.html').then(()=>{document.getElementById('result').textContent='unexpected success'},()=>{document.getElementById('result').textContent='resource refused'})\">refuse child frame</button>").encode()
            files = [("frame.html", "text/html", frame),
                     ("index.html", "text/html", html),
                     ("query.html", "text/html", query),
                     ("save.bin", "application/octet-stream", b"authentic saved bytes\n"),
                     ("sub.html", "text/html", ("<title>Sub " + label + "</title><p>relative").encode())]
            payload = web.package([(p.encode(), m.encode(), b) for p, m, b in files], label=label.encode(), entry_index=1)
            reference = lambda p, record: bytes.fromhex(p["txid"])[::-1] + v.sha(record)
            data = v.prefix(7) + v.uint(0, 4) + payload
            pd, dd = proof(label + "-data", data)
            leaf = v.prefix(8) + v.uint(0, 2) + v.uint(1, 2) + v.uint(0, 4) + reference(pd, data)
            pl, dl = proof(label + "-leaf", leaf)
            root = (v.prefix(9) + v.uint(len(payload), 8) + v.sha(payload) + v.uint(1, 4)
                    + v.uint(1, 2) + bytes(2) + b"URMAWEB1" + reference(pl, leaf))
            pr, dr = proof(label + "-root", root)
            fixtures[label] = {"root": pr["txid"], "author": pr["author"], "payload_hex": payload.hex(),
                               "payload_sha256": v.sha(payload).hex(), "proofs": [dd, dl, dr],
                               "files": [{"path": p, "mime": m, "hex": b.hex(), "sha256": v.sha(b).hex(),
                                          "length": len(b)} for p, m, b in files]}
    result = {"schema": 1, "core_writer_commit": commit, "public_fixture_key": 3,
              "provenance": "Independent BIP340/341 and URMAWEB1 Python writers; public test key only. No node or inclusion evidence.",
              "publications": fixtures}
    args.output.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
