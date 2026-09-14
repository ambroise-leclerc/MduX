#!/usr/bin/env python3
"""Drive the embedded Studio in a real headless browser against a real mdux-preview and GPU.

This is the bounded real-renderer smoke test for #327: every frame the page shows comes from the
production offscreen renderer, every edit goes through the service's compile, and the proposal is
pushed to a local bare repository with real git. Timeouts bound every wait.
"""
import json
import os
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from DevTools import Browser, Page  # noqa: E402

binary, repo, executable = sys.argv[1:4]
recipe = "recipes/screen/endoscope-monitor.toml"
source_path = "recipes/screen/endoscope-monitor/EndoscopeMonitor.medui"
git_env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)
ctrl = 2


def git(*args, cwd=None):
    result = subprocess.run(["git", *args], cwd=cwd, env=git_env, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, (args, result.stderr)
    return result.stdout.strip()


def bits(value):
    return {"bits": struct.unpack("<I", struct.pack("<f", value))[0]}


with tempfile.TemporaryDirectory(prefix="mdux-studio-test-") as work:
    work = Path(work)
    token = secrets.token_hex(32)
    (work / "token").write_text(token)
    root = work / "repo"
    shutil.copytree(Path(repo) / "recipes/screen", root / "recipes/screen")
    shutil.copytree(Path(repo) / "generated", root / "generated")
    git("init", "--quiet", "--initial-branch=develop", cwd=root)
    git("-c", "user.name=Studio Test", "-c", "user.email=studio@localhost", "add", "--all", cwd=root)
    git("-c", "user.name=Studio Test", "-c", "user.email=studio@localhost", "commit", "--quiet", "-m", "base", cwd=root)
    remote = work / "remote.git"
    git("init", "--quiet", "--bare", str(remote))
    git("push", "--quiet", str(remote), "develop:refs/heads/develop", cwd=root)
    service_git = work / "proposals.git"
    git("init", "--quiet", "--bare", str(service_git))
    git("--git-dir", str(service_git), "config", "user.name", "Studio Test")
    git("--git-dir", str(service_git), "config", "user.email", "studio@localhost")
    git("--git-dir", str(service_git), "remote", "add", "origin", str(remote))
    original = (root / source_path).read_bytes()

    server = subprocess.Popen([binary, "--root", str(root), "--token-file", str(work / "token"), "--proposal-git-dir", str(service_git)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    browser = None
    try:
        address = server.stdout.readline().strip()
        assert address.startswith("http://127.0.0.1:"), (address, server.poll())
        browser = Browser(executable, work / "profile")
        page = Page(browser, f"{address}/#token={token}")

        def settled(condition="true", description="the Studio to settle"):
            page.wait(f"document.body.dataset.busy === 'false' && ({condition})", description)

        def image_digest():
            return page.evaluate("(async () => { const s = document.getElementById('frame').getAttribute('src') || ''; "
                                 "const d = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s)); "
                                 "return Array.from(new Uint8Array(d)).map(b => b.toString(16).padStart(2, '0')).join(''); })()")

        def field_value(name):
            return page.evaluate(f"document.getElementById('field-{name}')?.value ?? null")

        # Connect: the token leaves the address bar, the screen loads, and the compile is valid.
        page.wait("document.getElementById('workspace').hidden === false", "the workspace")
        assert page.evaluate("location.hash") == "", "the token fragment must be removed from the address bar"
        settled("document.body.dataset.compile === 'valid'", "the first compile")
        assert page.evaluate("document.getElementById('screen').value") == recipe

        # No frame is invented: the fixture skeleton names every binding with null, and the service refuses it.
        assert page.evaluate("document.body.dataset.frame") == "none"
        skeleton = json.loads(page.evaluate("document.getElementById('fixture').value"))
        assert skeleton["readings"] == {"insufflation-pressure": None} and skeleton["clock"] is None, skeleton
        assert "PRV002" in page.evaluate("document.getElementById('frame-note').textContent")

        fixture = {
            "readings": {"insufflation-pressure": 123},
            "statuses": {"classifier-state": 1},
            "fields": {"patient-id": {"text": "ABC123", "caret": 6}},
            "signals": {"ECG_LEAD_II": {"samples": [bits(-0.5), bits(0.5), bits(-0.25)], "minimum": bits(-1), "maximum": bits(1), "strokeWidth": 2}},
            "viewports": {"ENDOSCOPE_PRIMARY": {"rows": [[bits(0), bits(1)]], "bins": 2, "minimum": bits(0), "maximum": bits(1),
                                                "lowColor": {"r": 0, "g": 0, "b": 0, "a": 255}, "highColor": {"r": 255, "g": 0, "b": 0, "a": 255}}},
            "clock": {"year": 2026, "month": 9, "day": 14, "hour": 12, "minute": 0, "second": 0, "colorToken": "Theme.Colors.Nominal"},
        }
        page.type_into("#fixture", json.dumps(fixture), enter=False)
        page.click("#apply-fixture")
        settled("document.body.dataset.frame === 'rendered'", "a rendered frame")
        assert page.evaluate("document.getElementById('frame').naturalWidth") == 1280
        rendered = image_digest()

        # Select a node on the canvas and edit it through the inspector.
        page.click('.box[data-id="ecg-lead-ii"]')
        page.wait("document.body.dataset.selected === 'ecg-lead-ii' && document.getElementById('field-height') !== null", "selection")
        page.type_into("#field-height", "100px")
        settled("document.getElementById('field-height')?.value === '100px' && document.body.dataset.frame === 'rendered'", "the height edit")
        assert page.evaluate("document.body.dataset.compile") == "valid"
        assert image_digest() != rendered, "a valid edit must reach the production renderer's pixels"

        # Move by dragging; the document position follows the pointer in surface pixels.
        scale = page.evaluate("parseFloat(document.getElementById('stage').style.width) / 1280")
        x, y = page.center('.box[data-id="ecg-lead-ii"]')
        page.drag((x, y), (x, y + 20 * scale))
        settled("document.getElementById('field-position')?.value === '0px, 612px'", "the drag")
        assert page.evaluate("document.body.dataset.compile") == "valid"
        moved = image_digest()

        # Resize with the handle.
        hx, hy = page.center('.box[data-id="ecg-lead-ii"] .handle')
        page.drag((hx, hy), (hx, hy - 10 * scale))
        settled("document.getElementById('field-height')?.value === '90px' && document.body.dataset.frame === 'rendered'", "the resize")
        valid = image_digest()
        assert valid != moved

        # An invalid edit is distinguished from the last valid preview, which stays on screen.
        x, y = page.center('.box[data-id="ecg-lead-ii"]')
        page.drag((x, y), (x, y + 200 * scale))
        settled("document.body.dataset.compile === 'invalid'", "the invalid move")
        assert page.evaluate("!document.getElementById('banner').hidden && document.getElementById('stage').classList.contains('invalid')")
        assert page.evaluate("document.querySelector('.proposed') !== null"), "the proposed geometry must be outlined"
        assert page.evaluate("document.getElementById('propose').disabled"), "an invalid edit cannot be proposed"
        assert page.evaluate("[...document.querySelectorAll('#diagnostics [data-code]')].some(e => e.dataset.code.startsWith('MEDUI-E'))")
        assert image_digest() == valid, "the last valid frame must remain, not a frame of the invalid edit"

        # Undo and redo, by keyboard and by button.
        page.key("z", "KeyZ", 90, ctrl)
        settled("document.body.dataset.compile === 'valid' && document.getElementById('banner').hidden", "undo")
        assert field_value("position") == "0px, 612px"
        page.key("z", "KeyZ", 90, ctrl | 8)
        settled("document.body.dataset.compile === 'invalid'", "redo")
        page.click("#undo")
        settled("document.body.dataset.compile === 'valid' && document.body.dataset.frame === 'rendered'", "undo button")
        assert image_digest() == valid

        # Guard: a required field cannot be removed from the inspector.
        page.evaluate("(() => { const s = document.getElementById('field-color'); s.value = ''; s.dispatchEvent(new Event('change')); })()")
        assert "required" in page.evaluate("document.getElementById('field-color').parentElement.querySelector('.field-error').textContent")
        assert field_value("color") == "Theme.Colors.Nominal"

        # Palette: a new component gets geometry only, so the compiler reports its missing content fields.
        page.key("Escape", "Escape", 27)
        page.click('#palette [data-component="Label"]')
        settled("document.body.dataset.compile === 'invalid' && document.body.dataset.selected === 'label-1'", "the palette insertion")
        assert page.evaluate("document.getElementById('field-text').parentElement.classList.contains('missing')")
        page.key("z", "KeyZ", 90, ctrl)
        settled("document.body.dataset.compile === 'valid'", "undoing the insertion")

        # Propose: review, acknowledge the comment loss, submit, and find the branch on the remote.
        page.click("#propose")
        page.wait("document.getElementById('proposal').open", "the proposal dialog")
        page.type_into("#proposal-issue", "327", enter=False)
        page.type_into("#proposal-title", "Shorten the ECG trace", enter=False)
        assert page.evaluate("document.getElementById('proposal-slug').value") == "shorten-the-ecg-trace"
        page.click("#proposal-review-button")
        page.wait("document.getElementById('proposal-result').textContent.startsWith('Review complete')", "the proposal review")
        assert page.evaluate("!document.getElementById('ack-comments-row').hidden && document.getElementById('proposal-submit').disabled")
        page.click("#ack-comments")
        page.wait("!document.getElementById('proposal-submit').disabled", "acknowledgement to enable submission")
        page.click("#proposal-submit")
        page.wait("document.getElementById('proposal-branch') !== null", "the pushed branch", timeout=150)
        branch = page.evaluate("document.getElementById('proposal-branch').textContent")
        assert branch.startswith("327-shorten-the-ecg-trace-"), branch
        committed = git("--git-dir", str(remote), "show", f"refs/heads/{branch}:{source_path}")
        assert "height: 90px;" in committed and "position: 0px, 612px;" in committed, committed

        assert (root / source_path).read_bytes() == original, "the Studio must never write the served checkout"
        errors = page.errors()
        assert not errors, errors
        print("preview studio passed")
    finally:
        if browser is not None:
            browser.close()
        server.terminate()
        try:
            server.wait(timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
