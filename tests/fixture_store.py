"""Materialize public signed fixtures into a fresh test-owned store only."""
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).digest()


def load_fixture():
    return json.loads((Path(__file__).parent / "fixtures/authentic.json").read_text())


def materialize(store, fixture, networks=("litecoin-mainnet", "litecoin-testnet")):
    assert not store.exists(), "fixture requires a new, test-owned store"
    (store / "tx").mkdir(parents=True)
    (store / "objects").mkdir()
    for item in fixture["publications"].values():
        payload = bytes.fromhex(item["payload_hex"])
        assert digest(payload).hex() == item["payload_sha256"]
        (store / "objects" / item["payload_sha256"]).write_bytes(payload)
        for file in item["files"]:
            data = bytes.fromhex(file["hex"])
            assert len(data) == file["length"] and digest(data).hex() == file["sha256"]
            (store / "objects" / file["sha256"]).write_bytes(data)
        for proof in item["proofs"]:
            commit = bytes.fromhex(proof["commit_hex"])
            commit_id = digest(digest(commit))[::-1].hex()  # commits have no witness
            (store / "tx" / f"{commit_id}.bin").write_bytes(commit)
            (store / "tx" / f"{proof['txid']}.bin").write_bytes(bytes.fromhex(proof["reveal_hex"]))
        root = item["proofs"][-1]
        assert root["txid"] == item["root"]
        commit_id = digest(digest(bytes.fromhex(root["commit_hex"])))[::-1].hex()
        for network in networks:
            directory = store / "publications" / network
            directory.mkdir(parents=True, exist_ok=True)
            pointer = {"format": "URMA-WEB-STORE-2", "network": network, "root": item["root"],
                       "commit": commit_id, "tip_height": 1, "tip_hash": "11" * 32}
            (directory / f"{item['root']}.json").write_text(json.dumps(pointer))
