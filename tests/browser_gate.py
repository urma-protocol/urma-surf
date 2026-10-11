#!/usr/bin/env python3
"""Real GUI gate: private Xvfb/DBus/HOME, offline fixtures and strict mock CLI.

No implicit build, installed CLI, user store, node or live endpoint. All subprocess
lifetimes are bounded; final cleanup reaps test-owned process groups and removes
the temporary profile. This tests production GTK/WebKit callbacks, not a model.
"""
import argparse
import configparser
import json
import hashlib
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile

from fixture_store import load_fixture, materialize


G1, G2, G3 = "1" * 64, "2" * 64, "3" * 64


def setup(work, real_cli=None, evidence=None):
    fixture = load_fixture()
    (work / "fixture.json").write_text(json.dumps(fixture))
    materialize(work / "store", fixture)
    home = work / "home"
    (home / ".surf").mkdir(parents=True)
    (home / ".surf/urma-registries.ini").write_text(f"[active]\nltc={G1}\ntltc={G1}\n")
    names = {}
    for network in ("litecoin-mainnet", "litecoin-testnet"):
        for genesis in (G1, G2, G3):
            index = work / "names" / network / f"{genesis}.json"
            index.parent.mkdir(parents=True, exist_ok=True)
            index.write_text(json.dumps({"network": network, "registry": {"genesis": genesis,
                                                                        "names": {}}}))
    names.update({f"litecoin-mainnet/{G1}/alpha": "A", f"litecoin-mainnet/{G1}/beta": "B",
                  f"litecoin-mainnet/{G2}/alpha": "B", f"litecoin-mainnet/{G2}/beta": "C",
                  f"litecoin-mainnet/{G2}/savecheck": "B", f"litecoin-mainnet/{G1}/savecheck": "A",
                  f"litecoin-mainnet/{G3}/alpha": "C", f"litecoin-mainnet/{G3}/beta": "D",
                  f"litecoin-testnet/{G1}/alpha": "D", f"litecoin-testnet/{G1}/beta": "A",
                  f"litecoin-testnet/{G2}/alpha": "B", f"litecoin-testnet/{G2}/beta": "C"})
    state = {"names": names, "fetch": {}}
    (work / "state.json").write_text(json.dumps(state))
    mock = work / "mock-urma"
    shutil.copyfile(Path(__file__).with_name("mock_cli.py"), mock)
    mock.chmod(0o700)
    for directory in ("runtime", "state", "data", "config", "cache"):
        (work / directory).mkdir(mode=0o700)
    env = {"PATH": os.defpath, "LANG": "C.UTF-8", "HOME": str(home),
           "XDG_RUNTIME_DIR": str(work / "runtime"), "XDG_STATE_HOME": str(work / "state"),
           "XDG_DATA_HOME": str(work / "data"), "XDG_CONFIG_HOME": str(work / "config"),
           "XDG_CACHE_HOME": str(work / "cache"), "URMA_CONFIG": str(work / "missing-config"),
           "URMA_BIN": str(mock), "URMA_WEB_STORE": str(work / "store"),
           "URMA_NAMES_DIR": str(work / "names"), "URMA_RPC_URL": "http://127.0.0.1:1/",
           "URMA_NODE_AUTH_FILE": str(work / "missing-cookie"), "URMA_LOG_OUTPUT": "stderr",
           "BROWSER_TEST_WORK": str(work), "GTK_THEME": "Adwaita", "G_DEBUG": "fatal-warnings",
           "GSETTINGS_BACKEND": "memory", "GIO_USE_VFS": "local", "NO_AT_BRIDGE": "0", "GTK_MODULES": "atk-bridge",
           "PYTHONDONTWRITEBYTECODE": "1"}
    if real_cli:
        env["BROWSER_TEST_REAL_CLI"] = str(real_cli)
    if evidence:
        env["BROWSER_TEST_EVIDENCE"] = str(evidence)
    return env


def child(work, browser, focus=None):
    from gui_driver import Driver, wait
    fixture = load_fixture()["publications"]
    roots = {name: p["root"] for name, p in fixture.items()}
    url = lambda label, net="ltc", path="/": f"urma://{roots[label]}.{net}{path}"
    explicit = lambda genesis, name="alpha", net="ltc": f"urma://{genesis}.{name}.{net}/"
    ini = work / "home/.surf/urma-registries.ini"
    original_ini = ini.read_bytes()
    checks, skipped = [], []
    driver = Driver(work, browser, "urma://alpha.ltc/")

    def check(name, condition):
        assert condition, (name, driver.diagnostic())
        checks.append(name)
        print("PASS " + name, flush=True)

    def loaded(label, net="ltc", path="/"):
        driver.uri(url(label, net, path))
        driver.element(("Sub " if path == "/sub.html" else "Fixture ") + label, "document web", exact=True)

    def choose(genesis, label, name="alpha", net="ltc", choice="Use temporarily"):
        driver.go(explicit(genesis, name, net))
        driver.element(choice, "button", exact=True)
        driver.screenshot("registry-choice.png")
        driver.click(choice, "button", exact=True)
        loaded(label, net)

    def reset():
        driver.click("Use global", "button", exact=True)
        driver.text("temporary history scopes cleared")
        wait(lambda: not driver.showing("Temporary .ltc") and not driver.showing("Temporary .tltc"),
             "reset hides temporary indicator")

    try:
        loaded("A")
        check("global initial name", driver.copied("urma://alpha.ltc/"))
        choose(G2, "B")
        wait(lambda: driver.showing("Temporary .ltc"), "visible temporary ltc indicator")
        driver.screenshot("temporary-indicator.png")
        check("temporary copy is explicit", driver.copied(explicit(G2)))
        check("temporary does not persist global", ini.read_bytes() == original_ini)
        driver.go("urma://beta.ltc/")
        loaded("C")
        check("same temporary registry resolves a different root", driver.copied(explicit(G2, "beta")))
        if focus:
            if focus == "contexts":
                # Old B history belongs to the previous scope epoch. A newer
                # G3 tuple must survive Back and drive the next name lookup.
                reset()
                choose(G3, "C")
                driver.keys("alt+Left")
                loaded("B")
                driver.go("urma://beta.ltc/")
                loaded("D")
                check("old-epoch Back preserves newer temporary H", driver.copied(explicit(G3, "beta")))

                # Shell publication addresses intentionally reset tab context.
                driver.go(url("A"))
                loaded("A")
                check("successful shell PUB clears temporary scope and name", driver.copied(url("A")) and
                      not driver.showing("Temporary .ltc"))
                driver.go("urma://beta.ltc/")
                loaded("B")
                check("name after shell PUB uses global registry", driver.copied("urma://beta.ltc/"))

                # Global persistence is a separate accepted side effect before
                # fetch. Failed fetch must keep the old page and G3 tab tuple.
                choose(G3, "C")
                unknown = "f" * 64
                state = json.loads((work / "state.json").read_text())
                state["raw_names"] = {f"litecoin-mainnet/{G2}/globalfail": unknown}
                state["fetch"][unknown] = "refuse"
                state["fetch_epoch"] = 17
                (work / "state.json").write_text(json.dumps(state))
                driver.go(explicit(G2, "globalfail"))
                driver.click("Switch global", "button", exact=True)
                driver.text("fixture fetch refused [17]")
                loaded("C")
                selected = configparser.ConfigParser()
                selected.read(ini)
                check("Global fetch failure persists selected G but preserves old page and temporary H",
                      selected["active"]["ltc"] == G2 and selected["active"]["tltc"] == G1 and
                      driver.showing("Temporary .ltc") and driver.copied(explicit(G3)))
                driver.go("urma://beta.ltc/")
                loaded("D")
                check("name after Global fetch failure still uses committed temporary H",
                      driver.copied(explicit(G3, "beta")))
                reset()
                driver.go("urma://beta.ltc/")
                loaded("C")
                check("reset after Global fetch failure uses persisted new G", driver.copied("urma://beta.ltc/"))
                print(json.dumps({"gate": "focused-contexts", "passed": len(checks), "checks": checks}))
                return
            if focus == "reset":
                choose(G3, "C")
                unknown = "f" * 64
                state = json.loads((work / "state.json").read_text())
                state["raw_names"] = {f"litecoin-mainnet/{G2}/pending": unknown}
                state["fetch"][unknown] = "wait"
                state["fetch_epoch"] = 1
                (work / "state.json").write_text(json.dumps(state))
                driver.go(explicit(G2, "pending"))
                driver.click("Use temporarily", "button", exact=True)
                wait(lambda: (work / f"fetch-{unknown}.started").exists(), "focused reset fetch started")
                reset()
                driver.go("urma://alpha.ltc/")
                loaded("A")
                check("focused reset keeps authentic new document/global copy", driver.copied("urma://alpha.ltc/"))
                (work / f"fetch-{unknown}.release").write_text("release")
                wait(lambda: (work / f"fetch-{unknown}.finished").exists(), "focused stale fetch finishes")
                loaded("A")
                check("focused stale completion leaves global context", driver.copied("urma://alpha.ltc/"))
                print(json.dumps({"gate": "focused-reset", "passed": len(checks), "checks": checks}))
                return
            if focus == "refusal":
                driver.click("failed publication", "link", exact=True)
                driver.text("fixture fetch refused")
                loaded("C")
                check("focused refusal preserves copy", driver.copied(explicit(G2, "beta")))
                print(json.dumps({"gate": "focused-refusal", "passed": len(checks), "checks": checks}))
                return
            driver.click("relative" if focus == "relative" else "query", "link", exact=True)
            if focus == "relative":
                loaded("C", path="/sub.html")
                check("focused relative copy", driver.copied(explicit(G2, "beta") + "sub.html"))
            else:
                driver.element("Query C", "document web", exact=True)
                driver.element("Refresh", "button", exact=True)
                check("focused parser-time query copy", driver.copied(explicit(G2, "beta") + "query.html?step=1"))
            driver.element("Refresh", "button", exact=True)
            print(json.dumps({"gate": "focused-" + focus, "passed": len(checks), "checks": checks}))
            return
        driver.go("urma://alpha.tltc/")
        loaded("D", "tltc")
        wait(lambda: driver.showing("Temporary .ltc"), "visible temporary ltc indicator")
        driver.screenshot("temporary-dormant.png")
        check("other network uses global while tuple is dormant", driver.copied("urma://alpha.tltc/"))
        driver.keys("alt+Left")
        loaded("C")
        check("back restores temporary binding", driver.copied(explicit(G2, "beta")))
        driver.keys("alt+Right")
        loaded("D", "tltc")
        check("forward restores other-network binding", driver.copied("urma://alpha.tltc/"))
        driver.keys("alt+Left")
        loaded("C")
        driver.keys("F5")
        loaded("C")
        check("reload retains temporary binding", driver.copied(explicit(G2, "beta")))
        driver.click("relative", "link", exact=True)
        loaded("C", path="/sub.html")
        check("relative navigation retains scope", driver.copied(explicit(G2, "beta") + "sub.html"))
        driver.keys("alt+Left")
        loaded("C")
        driver.click("query", "link", exact=True)
        driver.element("Query C", "document web", exact=True)
        # Observe FINISHED chrome before any shortcut can cancel a view load.
        driver.element("Refresh", "button", exact=True)
        check("parser-time query finishes loading", True)
        check("parser-time pushState query preserves copied binding", driver.copied(explicit(G2, "beta") + "query.html?step=1"))
        driver.click("query after load", "button", exact=True)
        driver.element("Refresh", "button", exact=True)
        check("after-load pushState query preserves copied binding", driver.copied(explicit(G2, "beta") + "query.html?step=2"))
        driver.go("urma://beta.ltc/")
        loaded("C")
        driver.click("frames", "link", exact=True)
        driver.element("Frames C", "document web", exact=True)
        check("frame parent keeps publication context", driver.copied(explicit(G2, "beta") + "frame.html"))
        count_before = len(driver.calls())
        driver.click("refuse child frame", "button", exact=True)
        driver.text("resource refused")
        driver.element("Frames C", "document web", exact=True)
        driver.element("Refresh", "button", exact=True)
        check("undeclared child-frame attempt and failed fetch preserve main document/context",
              driver.copied(explicit(G2, "beta") + "frame.html"))
        check("undeclared subframe does not trigger publication fetch",
              not any(c["args"][:2] == ["web", "fetch"] for c in driver.calls()[count_before:]))
        driver.go("urma://beta.ltc/")
        loaded("C")

        driver.click("fragment", "link", exact=True)
        check("fragment copy retains tail", driver.copied(explicit(G2, "beta") + "#section"))
        driver.keys("alt+Left")
        loaded("C")
        check("fragment Back restores tail", driver.copied(explicit(G2, "beta")))
        driver.keys("alt+Right")
        check("fragment Forward restores tail", driver.copied(explicit(G2, "beta") + "#section"))
        driver.keys("alt+Left")
        loaded("C")
        count_before = len(driver.calls())
        driver.click("failed publication", "link", exact=True)
        driver.text("fixture fetch refused")
        check("in-page direct PUB failure preserves document and temporary tuple",
              driver.atom() == url("C") and driver.copied(explicit(G2, "beta")))
        check("direct PUB failure never resolves through names",
              not any(c["args"][:2] == ["names", "resolve"] for c in driver.calls()[count_before:]))

        # Tampered declared download bytes fail locally; no URI handler/CLI fallback.
        saved = next(f for f in fixture["C"]["files"] if f["path"] == "save.bin")
        object_path = work / "store/objects" / saved["sha256"]
        original_object = object_path.read_bytes()
        object_path.write_bytes(original_object + b"tamper")
        count_before = len(driver.calls())
        driver.click("save", "link", exact=True)
        driver.text("failed re-verification")
        check("declared save failure preserves document", driver.atom() == url("C"))
        check("declared save failure has no fetch fallback",
              not any(c["args"][:2] == ["web", "fetch"] for c in driver.calls()[count_before:]))
        object_path.write_bytes(original_object)

        # Real document _blank action creates a derived tab, with an independent copy.
        driver.click("child beta", "link", exact=True)
        loaded("C")
        check("child tab inherits temporary choice", driver.copied(explicit(G2, "beta")))
        reset()
        driver.go("urma://alpha.ltc/")
        loaded("A")
        driver.keys("ctrl+1")
        loaded("C")
        check("child reset does not mutate opener scope", driver.copied(explicit(G2, "beta")))
        driver.click("script child", "button", exact=True)
        loaded("C")
        check("clicked window.open child inherits temporary choice", driver.copied(explicit(G2, "beta")))
        driver.keys("ctrl+w")
        driver.keys("ctrl+1")
        loaded("C")
        driver.keys("ctrl+t")
        driver.go("urma://alpha.ltc/")
        loaded("A")
        check("independent blank tab uses global", driver.copied("urma://alpha.ltc/") and
              not driver.showing("Temporary .ltc") and not driver.showing("Temporary .tltc"))
        driver.keys("ctrl+w")
        driver.keys("ctrl+1")
        loaded("C")

        # Cancel and unavailable names must not load a different registry or erase scope.
        driver.go(explicit(G3))
        driver.element("Use temporarily", "button", exact=True)
        driver.screenshot("registry-choice.png")
        driver.click("Cancel", "button", exact=True)
        driver.text("cancel")
        check("explicit cancel preserves document and tuple", driver.atom() == url("C") and driver.copied(explicit(G2, "beta")))
        choose(G3, "C")
        check("explicit different registry replaces tuple", driver.copied(explicit(G3)))
        driver.go(explicit(G2, "missing"))
        driver.click("Use temporarily", "button", exact=True)
        driver.text("cannot open missing.ltc")
        check("resolution failure preserves committed tuple", driver.atom() == url("C") and driver.copied(explicit(G3)))

        # A temporary tuple on another network replaces, rather than accumulates, the old one.
        choose(G2, "B", net="tltc")
        wait(lambda: driver.showing("Temporary .tltc"), "visible temporary tltc indicator")
        driver.go("urma://alpha.ltc/")
        loaded("A")
        check("single tuple replacement returns old network to global", driver.copied("urma://alpha.ltc/"))
        wait(lambda: driver.showing("Temporary .tltc"), "visible temporary tltc indicator")
        reset()
        driver.keys("alt+Left")
        loaded("B", "tltc")
        driver.go("urma://beta.tltc/")
        loaded("A", "tltc")
        check("reset epoch prevents history scope resurrection", driver.copied("urma://beta.tltc/"))

        # A failed GLOBAL persistence must neither load selected G nor silently use old G.
        driver.go("urma://alpha.ltc/")
        loaded("A")
        original = ini.read_bytes()
        ini.unlink()
        ini.mkdir()
        count_before = len(driver.calls())
        driver.go(explicit(G2))
        driver.click("Switch global", "button", exact=True)
        driver.text("cannot")
        check("registry read failure keeps previous document", driver.atom() == url("A"))
        check("registry read failure does not load or fetch fallback publication",
              not any(c["args"][:2] in (["web", "fetch"], ["web", "manifest"])
                      for c in driver.calls()[count_before:]))
        ini.rmdir()
        ini.write_bytes(original)

        # Preserve readable old G, then fail the actual checked SAVE (not read).
        if os.geteuid() == 0:
            skipped.append("unwritable registry save: root bypasses Unix permission test")
        else:
            directory_mode, file_mode = ini.parent.stat().st_mode & 0o777, ini.stat().st_mode & 0o777
            ini.chmod(0o400)
            ini.parent.chmod(0o500)
            try:
                assert ini.read_bytes() == original
                count_before = len(driver.calls())
                # A fresh name bypasses the resolver's five-second cache.
                driver.go(explicit(G2, "savecheck"))
                driver.click("Switch global", "button", exact=True)
                driver.text("Permission denied")
                loaded("A")
                attempted = driver.calls()[count_before:]
                # Chrome may revalidate the prior page's alpha binding. Those
                # lookups are not fallback resolution of the selected savecheck.
                resolutions = [c for c in attempted if c["args"][:2] == ["names", "resolve"] and
                               c["args"][-1] == "savecheck"]
                check("unwritable SAVE keeps readable old registry bytes", ini.read_bytes() == original)
                check("unwritable SAVE resolves selected name only through H",
                      len(resolutions) == 1 and Path(resolutions[0]["args"][resolutions[0]["args"].index("--index") + 1]).stem == G2)
                check("unwritable SAVE loads neither H nor fallback publication",
                      driver.copied("urma://alpha.ltc/") and not any(
                          c["args"][:2] in (["web", "manifest"], ["web", "fetch"]) for c in attempted))
            finally:
                ini.parent.chmod(directory_mode)
                ini.chmod(file_mode)

        # Exercise asynchronous fetch refusal and close/reset while a fetch is outstanding.
        unknown = "f" * 64
        state = json.loads((work / "state.json").read_text())
        state["names"][f"litecoin-mainnet/{G2}/pending"] = "UNKNOWN"
        # Mock adapter accepts this explicit raw fixture root separately from signed fixtures.
        state["raw_names"] = {f"litecoin-mainnet/{G2}/pending": unknown}
        state["fetch"][unknown] = "wait"
        state["fetch_epoch"] = 1
        (work / "state.json").write_text(json.dumps(state))
        driver.go(explicit(G2, "pending"))
        driver.click("Use temporarily", "button", exact=True)
        wait(lambda: (work / f"fetch-{unknown}.started").exists(), "pending fetch started")
        (work / f"fetch-{unknown}.release").write_text("release")
        driver.text("fixture fetch refused [1]")
        check("failed pending fetch preserves previous document", driver.atom() == url("A"))
        check("failed pending fetch installs no temporary binding", driver.copied("urma://alpha.ltc/"))
        (work / f"fetch-{unknown}.release").unlink()
        (work / f"fetch-{unknown}.started").unlink()
        (work / f"fetch-{unknown}.finished").unlink()
        state["fetch_epoch"] = 2
        (work / "state.json").write_text(json.dumps(state))
        # Reset is visible for a committed temporary tuple; a pending choice
        # alone has intentionally installed no scope or reset button.
        choose(G3, "C")
        driver.go(explicit(G2, "pending"))
        driver.click("Use temporarily", "button", exact=True)
        wait(lambda: (work / f"fetch-{unknown}.started").exists(), "reset fetch started")
        reset()
        driver.go("urma://alpha.ltc/")
        loaded("A")
        driver.go(explicit(G2, "pending"))
        driver.click("Use temporarily", "button", exact=True)
        (work / f"fetch-{unknown}.release").write_text("release")
        wait(lambda: (work / f"fetch-{unknown}.finished").exists(), "repeated same-URI fetch completes")
        driver.text("fixture fetch refused [2]")
        loaded("A")
        check("reset/new navigation/repeated same-URI refusal cannot resurrect temporary scope",
              driver.copied("urma://alpha.ltc/"))
        (work / f"fetch-{unknown}.release").unlink()
        (work / f"fetch-{unknown}.started").unlink()
        (work / f"fetch-{unknown}.finished").unlink()
        state["fetch_epoch"] = 3
        (work / "state.json").write_text(json.dumps(state))
        driver.keys("ctrl+t")
        driver.go(explicit(G2, "pending"))
        driver.click("Use temporarily", "button", exact=True)
        wait(lambda: (work / f"fetch-{unknown}.started").exists(), "child pending fetch started")
        # Ctrl-T left focus in the entry. Select it explicitly, then Escape
        # returns to the view without stopping its outstanding fetch.
        driver.keys("ctrl+l", "Escape", "ctrl+w")
        driver.keys("ctrl+1")
        (work / f"fetch-{unknown}.release").write_text("release")
        loaded("A")
        driver.go("urma://beta.ltc/")
        loaded("B")
        check("closing pending tab leaves surviving callback/UI usable", driver.copied("urma://beta.ltc/"))
        check("temporary/refused choices never changed global ini", ini.read_bytes() == original_ini)
        driver.go(explicit(G2))
        driver.click("Switch global", "button", exact=True)
        loaded("B")
        selected = configparser.ConfigParser()
        selected.read(ini)
        check("successful Global persists selected registry and keeps other network",
              selected["active"]["ltc"] == G2 and selected["active"]["tltc"] == G1)
        check("successful Global installs committed name without temporary scope",
              driver.copied("urma://alpha.ltc/"))
        driver.go("urma://beta.ltc/")
        loaded("C")
        check("next short name uses newly selected global registry", driver.copied("urma://beta.ltc/"))
        check("only explicit offline mock commands called", all(
            c["args"][:2] in (["names", "resolve"], ["web", "manifest"], ["web", "fetch"])
            for c in driver.calls()))
        driver.keys("ctrl+2")
        driver.keys("ctrl+w")
        driver.keys("ctrl+w")
        wait(lambda: driver.child.poll() is not None, "last browser tab closes")
        check("browser exits cleanly", driver.child.returncode == 0)
        report = {"gate": "real-gui-offline-mock-cli", "passed": len(checks), "checks": checks, "skipped": skipped,
                  "fixture_sha256": hashlib.sha256(Path(__file__).with_name("fixtures").joinpath("authentic.json").read_bytes()).hexdigest(),
                  "browser_sha256": hashlib.sha256(browser.read_bytes()).hexdigest(),
                  "web_extension_sha256": hashlib.sha256((browser.parent / "webext-surf.so").read_bytes()).hexdigest(),
                  "manifest_backend": "actual offline CLI" if os.environ.get("BROWSER_TEST_REAL_CLI") else "mock",
                  "limitation": "names resolution is mocked; no chain inclusion, live-provider or universal callback-race evidence"}
        if os.environ.get("BROWSER_TEST_REAL_CLI"):
            report["cli_sha256"] = hashlib.sha256(Path(os.environ["BROWSER_TEST_REAL_CLI"]).read_bytes()).hexdigest()
        if os.environ.get("BROWSER_TEST_EVIDENCE"):
            (Path(os.environ["BROWSER_TEST_EVIDENCE"]) / "gui-result.json").write_text(json.dumps(report, indent=2))
        print(json.dumps(report))
    except BaseException:
        diagnostic = driver.diagnostic()
        if os.environ.get("BROWSER_TEST_EVIDENCE"):
            directory = Path(os.environ["BROWSER_TEST_EVIDENCE"])
            (directory / "diagnostic.json").write_text(json.dumps(diagnostic, indent=2))
            shutil.copyfile(work / "browser.log", directory / "browser-trace.log")
        print(json.dumps(diagnostic, indent=2), file=sys.stderr)
        raise
    finally:
        driver.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", type=Path, required=True)
    parser.add_argument("--urma", type=Path, help="delegate offline web manifest to this real CLI")
    parser.add_argument("--evidence", type=Path, help="save fixture-only screenshots and gate logs here")
    parser.add_argument("--focus", choices=("relative", "query", "refusal", "reset", "contexts"), help="bounded focused navigation diagnostic")
    parser.add_argument("--child", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    browser = args.browser.resolve(strict=True)
    if args.child:
        child(args.child, browser, args.focus)
        return
    with tempfile.TemporaryDirectory(prefix="urma-browser-gui-") as temporary:
        work = Path(temporary)
        evidence = args.evidence.resolve() if args.evidence else None
        if evidence:
            evidence.mkdir(parents=True, exist_ok=True)
        env = setup(work, args.urma.resolve(strict=True) if args.urma else None, evidence)
        command = ["dbus-run-session", "--", sys.executable, str(Path(__file__).resolve()),
                   "--browser", str(browser), "--child", str(work)]
        if args.focus:
            command.extend(["--focus", args.focus])
        process = subprocess.Popen(command, env=env, cwd=work, start_new_session=True)
        try:
            result = process.wait(timeout=180)
            assert result == 0, f"GUI gate exit {result}"
        finally:
            # All inner children inherit this owned group. Cleanup also runs
            # when the deadline expires before the inner finally can execute.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait(timeout=3)
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass


if __name__ == "__main__":
    main()
