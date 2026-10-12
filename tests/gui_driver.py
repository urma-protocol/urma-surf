"""Bounded real GTK/WebKit controls on a private Xvfb and accessibility bus."""
import json
import os
from pathlib import Path
import re
import select
import signal
import subprocess
import time

import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def wait(predicate, description, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError("deadline: " + description)


class Driver:
    def __init__(self, work, browser, uri):
        self.work, self.browser = work, browser
        self.processes, self.trace = [], []
        self.env = dict(os.environ)
        self.blog = self.xlog = None
        try:
            self.start(uri)
        except BaseException:
            self.finish()
            raise

    def start(self, uri):
        work, browser = self.work, self.browser
        read_fd, write_fd = os.pipe()
        try:
            self.xlog = (work / "xvfb.log").open("w")
            xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0",
                                      "1280x900x24", "-nolisten", "tcp"],
                                     pass_fds=(write_fd,), stdout=self.xlog, stderr=self.xlog,
                                     )
            self.processes.append(xvfb)
            assert select.select([read_fd], [], [], 8)[0], "Xvfb display allocation deadline"
            display = os.read(read_fd, 64).decode().strip()
            assert display.isdigit(), display
            self.env["DISPLAY"] = ":" + display
            os.environ["DISPLAY"] = self.env["DISPLAY"]
            self.env["ATSPI_DBUS_IMPLEMENTATION"] = "dbus-daemon"
            os.environ["ATSPI_DBUS_IMPLEMENTATION"] = "dbus-daemon"
            self.run("dbus-update-activation-environment", "DISPLAY", "ATSPI_DBUS_IMPLEMENTATION")
            # AT-SPI calls may activate its registry; the private display must
            # already exist and be exported to D-Bus before the first call.
            Atspi.set_timeout(500, 1500)
            Atspi.init()
            Atspi.get_desktop(0)
        finally:
            os.close(read_fd)
            os.close(write_fd)
        self.blog = (work / "browser.log").open("w")
        self.child = subprocess.Popen([str(browser)] + ([uri] if uri is not None else []), cwd=work, env=self.env,
                                      stdout=self.blog, stderr=self.blog, )
        self.processes.append(self.child)
        self.window = wait(self.find_window, "browser window")
        self.run("xdotool", "windowsize", self.window, "1280", "900")

    def run(self, *args, expected=0):
        result = subprocess.run([str(a) for a in args], env=self.env, cwd=self.work,
                                capture_output=True, text=True, timeout=5)
        if expected is not None:
            assert result.returncode == expected, (args, result.returncode, result.stderr)
        return result

    def find_window(self):
        result = self.run("xdotool", "search", "--pid", self.child.pid,
                          "--class", "Surf", expected=None)
        for window in result.stdout.splitlines():
            properties = self.run("xprop", "-id", window, expected=None)
            if properties.returncode == 0 and "_SURF_URI" in properties.stdout:
                return window
        return None

    def atom(self):
        text = self.run("xprop", "-id", self.window, "_SURF_URI").stdout
        match = re.search(r'= "(.*)"', text)
        return match[1] if match else ""

    def uri(self, expected):
        wait(lambda: self.atom() == expected, "URI " + expected)
        self.trace.append({"uri": expected})

    def keys(self, *keys):
        self.run("xdotool", "windowfocus", "--sync", self.window)
        self.run("xdotool", "key", "--clearmodifiers", *keys)

    def go(self, uri):
        self.trace.append({"go": uri})
        self.run("xprop", "-id", self.window, "-f", "_SURF_GO", "8u", "-set", "_SURF_GO", uri)

    def tree(self):
        result, queue = [], []
        desktop = Atspi.get_desktop(0)
        for index in range(desktop.get_child_count()):
            child = desktop.get_child_at_index(index)
            if child and child.get_process_id() == self.child.pid:
                queue.append((child, 0))
        while queue and len(result) < 1500:
            node, depth = queue.pop(0)
            try:
                result.append((node, node.get_name() or "", node.get_role_name()))
                if depth < 20:
                    for index in range(node.get_child_count()):
                        child = node.get_child_at_index(index)
                        if child:
                            queue.append((child, depth + 1))
            except GLib.Error:
                # A page or dialog may disappear during an accessibility query.
                # Polling retries; failure to observe the target remains fatal.
                continue
        return result

    def names(self):
        return [(name, role) for _, name, role in self.tree()]

    def showing(self, name):
        for node, text, _ in self.tree():
            if text == name:
                states = node.get_state_set()
                if states.contains(Atspi.StateType.SHOWING) and states.contains(Atspi.StateType.VISIBLE):
                    return True
        return False

    def element(self, name, role=None, exact=False):
        def find():
            for node, text, kind in self.tree():
                if (text == name if exact else name.lower() in text.lower()) and (not role or role in kind):
                    if kind == "button":
                        states = node.get_state_set()
                        if not (states.contains(Atspi.StateType.SHOWING) and states.contains(Atspi.StateType.VISIBLE)):
                            continue
                    return node
            return None
        return wait(find, "accessible " + name)

    def click(self, name, role=None, button=1, exact=False):
        node = self.element(name, role, exact)
        extent = node.get_component_iface().get_extents(Atspi.CoordType.SCREEN)
        assert extent.width > 0 and extent.height > 0, (name, extent)
        self.run("xdotool", "mousemove", extent.x + extent.width // 2,
                 extent.y + extent.height // 2, "click", button)
        self.run("xdotool", "mousemove", "1270", "890")
        self.trace.append({"click": name, "button": button})

    def texts(self):
        result = []
        for node, name, role in self.tree():
            result.append(name)
            interface = node.get_text_iface()
            if interface:
                try:
                    result.append(Atspi.Text.get_text(interface, 0, -1))
                except GLib.Error:
                    pass
        return result

    def text(self, expected):
        wait(lambda: any(expected.lower() in text.lower() for text in self.texts()),
             "visible text " + expected)

    def copy(self, explicit=False):
        self.keys("ctrl+shift+u" if explicit else "ctrl+shift+y")
        # xclip reads only the private Xvfb clipboard.
        return wait(lambda: self.run("xclip", "-o", "-selection", "clipboard",
                                     expected=None).stdout, "clipboard")

    def copied(self, expected, explicit=False):
        # Escape leaves an editable address entry; never send it to the view,
        # where it would stop a still-finishing document and mask load failures.
        for node, _, role in self.tree():
            if role == "text":
                states = node.get_state_set()
                if (states.contains(Atspi.StateType.FOCUSED) and
                        states.contains(Atspi.StateType.EDITABLE) and
                        states.contains(Atspi.StateType.SINGLE_LINE)):
                    self.keys("Escape")
                    break
        def matches():
            self.keys("ctrl+shift+u" if explicit else "ctrl+shift+y")
            return self.run("xclip", "-o", "-selection", "clipboard",
                            expected=None).stdout == expected
        wait(matches, "clipboard " + expected)
        return True

    def screenshot(self, name):
        directory = os.environ.get("BROWSER_TEST_EVIDENCE")
        if directory:
            self.run("import", "-window", "root", Path(directory) / name)

    def calls(self):
        path = self.work / "calls.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def finish(self):
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
        if self.blog:
            self.blog.close()
        if self.xlog:
            self.xlog.close()

    def diagnostic(self):
        rows = self.run("ps", "-eo", "pid=,ppid=,stat=,comm=", expected=None).stdout.splitlines()
        owned, children = {self.child.pid}, []
        for _ in range(8):
            for row in rows:
                parts = row.split(None, 3)
                if len(parts) == 4 and int(parts[1]) in owned and int(parts[0]) not in owned:
                    owned.add(int(parts[0]))
                    children.append(row.strip())
        return {"browser_children": children,"trace": self.trace, "calls": self.calls(),
                "browser_returncode": self.child.poll(),
                "uri": self.run("xprop", "-id", self.window, "_SURF_URI", expected=None).stdout,
                "browser_log": (self.work / "browser.log").read_text()[-8000:],
                "accessibles": self.names(), "visible_texts": self.texts(), "properties": self.run("xprop", "-id", self.window, expected=None).stdout}
