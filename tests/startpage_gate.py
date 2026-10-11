#!/usr/bin/env python3
"""Offline native start-page gate; private Xvfb/profile, no CLI or node calls."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

from browser_gate import setup


def child(work, browser):
    from gui_driver import Driver, wait, Atspi
    driver = Driver(work, browser, None)  # Really launch with no URL argument.
    checks = []

    def check(name, condition):
        assert condition, name
        checks.append(name)
        print('PASS ' + name, flush=True)

    def home():
        driver.uri('about:blank')
        wait(lambda: driver.showing('A new place to explore.'), 'visible native start page')
        wait(lambda: driver.showing('Refresh'), 'blank load finished')

    def focused_entry():
        return any(role == 'text' and node.get_state_set().contains(Atspi.StateType.FOCUSED)
                   and node.get_state_set().contains(Atspi.StateType.EDITABLE)
                   and node.get_state_set().contains(Atspi.StateType.SINGLE_LINE)
                   for node, _, role in driver.tree())

    try:
        home()
        check('launch without URL shows built-in start page at about:blank', True)
        check('start page has no publication verification or temporary registry indicator',
              not driver.showing('verified') and not driver.showing('Temporary .ltc'))
        driver.screenshot('start-page.png')
        driver.click('Open an address', 'button', exact=True)
        wait(focused_entry, 'address button focuses address entry')
        check('primary action focuses existing address bar', True)
        driver.keys('Escape')
        driver.click('Settings', 'button', exact=True)
        wait(lambda: driver.showing('NAME REGISTRY · URMA NAMES'), 'existing settings panel')
        check('Settings action opens existing native settings', True)
        driver.keys('Escape')
        driver.click('Shortcuts', 'button', exact=True)
        driver.element('Keyboard shortcuts', 'dialog', exact=True)
        check('Shortcuts action opens existing keyboard sheet', True)
        driver.click('Close', 'button', exact=True)
        driver.click('New tab', 'button', exact=True)
        home()
        check('page New tab action creates independent blank tab',
              not driver.showing('Temporary .ltc'))
        driver.keys('Escape', 'ctrl+w')
        home()
        driver.keys('ctrl+t')
        home()
        wait(focused_entry, 'new tab address focus')
        check('keyboard Ctrl-T shows start page', True)
        driver.keys('Escape', 'ctrl+w')
        home()
        driver.keys('ctrl+r')
        home()
        check('reload retains start page and about:blank', True)

        # File navigation demonstrates that this is a shell panel rather than
        # a replacement document, injected web script or synthetic web origin.
        page = work / 'destination.html'
        page.write_text("""<!doctype html><title>Local destination</title><h1>Local destination</h1>
<button onclick="const w=window.open('about:blank');if(w){w.document.write('<title>Page-created child</title><h1>Page-created child</h1>');w.document.close();}">Open page-created blank</button>""")
        driver.go(page.as_uri())
        driver.uri(page.as_uri())
        driver.element('Local destination', 'document web', exact=True)
        wait(lambda: not driver.showing('A new place to explore.'), 'home hidden after navigation')
        check('navigation displays actual document without start panel', driver.showing('local file'))
        driver.click('Open page-created blank', 'button', exact=True)
        driver.element('Page-created child', 'document web', exact=True)
        check('page-created about:blank is not covered by Browser start UI',
              not driver.showing('A new place to explore.'))
        driver.keys('ctrl+w')
        driver.uri(page.as_uri())
        driver.go('about:blank')
        home()
        check('explicit about:blank returns to start', True)
        driver.keys('alt+Left')
        driver.uri(page.as_uri())
        wait(lambda: not driver.showing('A new place to explore.'), 'Back hides home')
        check('Back from explicit blank restores actual destination', True)
        driver.keys('alt+Right')
        home()
        check('Forward restores explicit start presentation', True)
        driver.run('xdotool', 'windowsize', driver.window, '744', '620')
        wait(lambda: driver.showing('Open an address'), 'primary action on smaller window')
        driver.screenshot('start-page-compact.png')
        check('compact window retains address action', True)
        check('no CLI requests for start page, actions or local-file navigation', driver.calls() == [])
        result = {'checks': checks, 'count': len(checks),
                  'browser_sha256': hashlib.sha256(browser.read_bytes()).hexdigest(),
                  'scope': 'Native about:blank presentation and existing shell actions; no network/CLI tests.'}
        evidence = os.environ.get('BROWSER_TEST_EVIDENCE')
        if evidence:
            (Path(evidence) / 'startpage-result.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result), flush=True)
    except BaseException:
        evidence = os.environ.get('BROWSER_TEST_EVIDENCE')
        if evidence:
            (Path(evidence) / 'startpage-failure.json').write_text(json.dumps(driver.diagnostic(), indent=2) + '\n')
            driver.screenshot('start-page-failure.png')
        raise
    finally:
        driver.finish()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser', type=Path, required=True)
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--child', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    browser = args.browser.resolve(strict=True)
    if args.child:
        child(args.child, browser)
        return
    with tempfile.TemporaryDirectory(prefix='urma-startpage-') as directory:
        work = Path(directory)
        evidence = args.evidence.resolve() if args.evidence else None
        if evidence:
            evidence.mkdir(parents=True, exist_ok=True)
        env = setup(work, evidence=evidence)
        command = ['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                   '--browser', str(browser), '--child', str(work)]
        process = subprocess.Popen(command, cwd=work, env=env, start_new_session=True)
        try:
            assert process.wait(timeout=90) == 0, 'start-page gate failed'
        finally:
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


if __name__ == '__main__':
    main()
