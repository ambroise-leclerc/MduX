"""A minimal Chrome DevTools Protocol client using only the Python standard library.

The Studio browser test drives a real Chrome or Chromium through the same protocol browser automation
libraries use. Implementing the small part it needs - a WebSocket client and request/response
correlation - keeps test tooling free of third-party packages, in line with the repository's other
host-side Python tools.
"""
import base64
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import time


class WebSocket:
    """RFC 6455 client: text frames, fragmentation, ping/pong. No extensions."""

    def __init__(self, port, path):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=120)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET {path} HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        self.buffer = bytearray()
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("DevTools closed the handshake")
            self.buffer += chunk
        head, _, rest = bytes(self.buffer).partition(b"\r\n\r\n")
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError(head.decode(errors="replace"))
        self.buffer = bytearray(rest)

    def _frame(self, opcode, data):
        header = bytearray([0x80 | opcode])
        size = len(data)
        if size < 126:
            header.append(0x80 | size)
        elif size < 65536:
            header += bytes([0x80 | 126]) + struct.pack(">H", size)
        else:
            header += bytes([0x80 | 127]) + struct.pack(">Q", size)
        mask = os.urandom(4)
        repeated = (mask * (size // 4 + 1))[:size]
        masked = (int.from_bytes(data, "big") ^ int.from_bytes(repeated, "big")).to_bytes(size, "big") if size else b""
        self.sock.sendall(bytes(header) + mask + masked)

    def send(self, text):
        self._frame(0x1, text.encode())

    def _read(self, size):
        while len(self.buffer) < size:
            chunk = self.sock.recv(1 << 16)
            if not chunk:
                raise ConnectionError("DevTools connection closed")
            self.buffer += chunk
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data

    def receive(self):
        message = bytearray()
        while True:
            first, second = self._read(2)
            size = second & 0x7F
            if size == 126:
                size = struct.unpack(">H", self._read(2))[0]
            elif size == 127:
                size = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if second & 0x80 else None
            payload = self._read(size)
            if mask:
                payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
            opcode = first & 0x0F
            if opcode == 0x9:
                self._frame(0xA, payload)
                continue
            if opcode == 0x8:
                raise ConnectionError("DevTools sent close")
            if opcode in (0x0, 0x1, 0x2):
                message += payload
                if first & 0x80:
                    return message.decode()


class Browser:
    def __init__(self, executable, profile):
        self.profile = Path(profile)
        arguments = [executable, "--headless=new", "--remote-debugging-port=0", f"--user-data-dir={self.profile}",
                     "--no-first-run", "--no-default-browser-check", "--disable-extensions", "--disable-sync",
                     "--disable-background-networking", "--disable-component-update", "--window-size=1600,1000", "about:blank"]
        if sys.platform.startswith("linux"):
            # CI runners and containers commonly lack the user namespaces Chrome's sandbox needs. The
            # page under test is this repository's own loopback Studio.
            arguments.insert(1, "--no-sandbox")
        self.process = subprocess.Popen(arguments, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        active = self.profile / "DevToolsActivePort"
        deadline = time.monotonic() + 60
        while not (active.exists() and len(active.read_text().splitlines()) >= 2):
            if self.process.poll() is not None:
                raise RuntimeError("browser exited: " + self.process.stderr.read().decode(errors="replace")[-2000:])
            if time.monotonic() > deadline:
                raise TimeoutError("browser did not publish a DevTools port")
            time.sleep(0.1)
        port, path = active.read_text().splitlines()[:2]
        self.socket = WebSocket(int(port), path)
        self.next = 0
        self.events = []

    def send(self, method, params=None, session=None):
        self.next += 1
        message = {"id": self.next, "method": method, "params": params or {}}
        if session:
            message["sessionId"] = session
        self.socket.send(json.dumps(message))
        while True:
            reply = json.loads(self.socket.receive())
            if reply.get("id") == self.next:
                if "error" in reply:
                    raise RuntimeError(f"{method}: {reply['error']}")
                return reply.get("result", {})
            if "method" in reply:
                self.events.append(reply)

    def close(self):
        try:
            self.send("Browser.close")
            self.process.wait(timeout=15)
        except Exception:
            self.process.kill()
            self.process.wait()
        shutil.rmtree(self.profile, ignore_errors=True)


class Page:
    def __init__(self, browser, url, width=1600, height=1000):
        self.browser = browser
        target = browser.send("Target.createTarget", {"url": "about:blank"})["targetId"]
        self.session = browser.send("Target.attachToTarget", {"targetId": target, "flatten": True})["sessionId"]
        for domain in ("Page.enable", "Runtime.enable", "Log.enable"):
            self.call(domain)
        self.call("Emulation.setDeviceMetricsOverride", {"width": width, "height": height, "deviceScaleFactor": 1, "mobile": False})
        self.call("Page.navigate", {"url": url})

    def call(self, method, params=None):
        return self.browser.send(method, params, self.session)

    def evaluate(self, expression):
        result = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in result:
            raise RuntimeError(f"{expression}: {result['exceptionDetails']}")
        return result["result"].get("value")

    def wait(self, expression, description, timeout=90):
        deadline = time.monotonic() + timeout
        while True:
            try:
                # Coerce in the page: returnByValue serialises a DOM node as {}, which Python reads as false.
                if self.evaluate(f"Boolean({expression})") is True:
                    return
            except RuntimeError:
                pass
            if time.monotonic() > deadline:
                state = self.evaluate("JSON.stringify({...document.body.dataset, status: document.getElementById('status')?.textContent, "
                                      "note: document.getElementById('frame-note')?.textContent, banner: document.getElementById('banner')?.textContent, "
                                      "result: document.getElementById('proposal-result')?.textContent})")
                raise TimeoutError(f"timed out waiting for {description}; page state {state}")
            time.sleep(0.1)

    def center(self, selector):
        box = self.evaluate(f"(() => {{ const r = document.querySelector({json.dumps(selector)}).getBoundingClientRect(); "
                            "return [r.left + r.width / 2, r.top + r.height / 2]; })()")
        return box[0], box[1]

    def mouse(self, kind, x, y, pressed):
        self.call("Input.dispatchMouseEvent", {"type": kind, "x": x, "y": y, "button": "left",
                                               "buttons": 1 if pressed else 0, "clickCount": 1})

    def click(self, selector):
        x, y = self.center(selector)
        self.mouse("mouseMoved", x, y, False)
        self.mouse("mousePressed", x, y, True)
        self.mouse("mouseReleased", x, y, False)

    def drag(self, start, end, steps=8):
        self.mouse("mouseMoved", start[0], start[1], False)
        self.mouse("mousePressed", start[0], start[1], True)
        for i in range(1, steps + 1):
            self.mouse("mouseMoved", start[0] + (end[0] - start[0]) * i / steps, start[1] + (end[1] - start[1]) * i / steps, True)
        self.mouse("mouseReleased", end[0], end[1], False)

    def key(self, key, code, virtual, modifiers=0):
        for kind in ("keyDown", "keyUp"):
            self.call("Input.dispatchKeyEvent", {"type": kind, "key": key, "code": code, "windowsVirtualKeyCode": virtual, "modifiers": modifiers})

    def type_into(self, selector, text, enter=True):
        self.evaluate(f"(() => {{ const e = document.querySelector({json.dumps(selector)}); e.focus(); e.select(); }})()")
        self.call("Input.insertText", {"text": text})
        if enter:
            self.key("Enter", "Enter", 13)

    def errors(self):
        """Uncaught exceptions, console errors and browser log errors (including CSP violations).

        Network log entries are excluded: the Studio receives refusals such as 422 on purpose, and the
        test asserts those responses through what the page shows rather than through the log.
        """
        found = []
        for event in self.browser.events:
            if event.get("sessionId") != self.session:
                continue
            method, params = event["method"], event.get("params", {})
            if method == "Runtime.exceptionThrown":
                found.append(params["exceptionDetails"].get("text", "exception"))
            elif method == "Runtime.consoleAPICalled" and params.get("type") == "error":
                found.append(json.dumps(params.get("args")))
            elif method == "Log.entryAdded" and params["entry"].get("level") == "error" and params["entry"].get("source") != "network":
                found.append(params["entry"].get("text"))
        return found
