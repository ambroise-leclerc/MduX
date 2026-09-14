#!/usr/bin/env python3
"""Exercise the real authenticated server, including production GPU previews."""
import base64
import copy
import concurrent.futures
import threading
import http.client
import json
import os
from pathlib import Path
import secrets
import shutil
import struct
import subprocess
import sys
import tempfile

binary, repo, mode = sys.argv[1:4]
recipe = "recipes/screen/endoscope-monitor.toml"

def bits(value):
    return {"bits": struct.unpack("<I", struct.pack("<f", value))[0]}

with tempfile.TemporaryDirectory(prefix="mdux-preview-test-") as work:
    work = Path(work)
    token = secrets.token_hex(32)
    token_path = work / "token"
    token_path.write_text(token)
    # Copy input fixtures: the service must never change the working repository.
    root = work / "repo"
    shutil.copytree(Path(repo) / "recipes/screen", root / "recipes/screen")
    shutil.copytree(Path(repo) / "generated", root / "generated")
    env = dict(os.environ)
    if mode == "no-device":
        env["VK_DRIVER_FILES"] = str(work / "absent-icd.json")
        env["VK_ICD_FILENAMES"] = env["VK_DRIVER_FILES"]
    if mode == "contract":
        inside_token = root / "token"
        inside_token.write_text(token)
        rejected_start = subprocess.run([binary, "--root", str(root), "--token-file", str(inside_token)], capture_output=True, text=True, timeout=10)
        assert rejected_start.returncode != 0 and "outside" in rejected_start.stderr
    process = subprocess.Popen([binary, "--root", str(root), "--token-file", str(token_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    try:
        address = process.stdout.readline().strip()
        assert address.startswith("http://127.0.0.1:"), (address, process.poll(), process.stderr.read() if process.poll() is not None else "")
        port = int(address.rsplit(":", 1)[1])
        def call(route, body=None, headers=None):
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
            h = {"Authorization": "Bearer " + token, "Content-Type": "application/json"}
            h.update(headers or {})
            connection.request("POST" if body is not None else "GET", "/api/" + route,
                               json.dumps(body) if body is not None else None, h)
            response = connection.getresponse()
            raw = response.read()
            status = response.status
            connection.close()
            return status, json.loads(raw) if raw else None
        assert call("catalog", headers={"Authorization": "Bearer wrong"})[0] == 401
        assert call("catalog", headers={"Origin": "https://example.invalid"})[0] == 403
        assert call("catalog", headers={"Host": "example.invalid"})[0] == 403
        assert call("catalog")[0] == 200
        assert recipe in call("screens")[1]["screens"]
        request = {"schemaVersion": 1, "recipe": recipe}
        status, compiled = call("compile", request)
        assert status == 200, compiled
        source = (root / "recipes/screen/endoscope-monitor/EndoscopeMonitor.medui").read_text()
        assert call("compile", {**request, "source": "Screen {"})[0] == 422
        assert call("compile", {**request, "schemaVersion": 2})[0] == 400
        assert call("compile", {**request, "recipe": "recipes/screen/../../outside.toml"})[0] == 403
        assert call("compile", {**request, "recipe": str(root / recipe)})[0] == 403
        # A recipe's indirect source path must be confined too.
        escape = root / "recipes/screen/escape.toml"
        escape.write_text((root / recipe).read_text().replace("recipes/screen/endoscope-monitor/EndoscopeMonitor.medui", "../../outside.medui"))
        assert call("compile", {**request, "recipe": "recipes/screen/escape.toml"})[0] == 403
        try:
            (root / "recipes/screen/link.toml").symlink_to(root / recipe)
        except OSError:
            pass  # Windows hosts without symlink privilege still test traversal.
        else:
            assert call("compile", {**request, "recipe": "recipes/screen/link.toml"})[0] == 403
        assert call("compile", {**request, "source": "x" * 4194304})[0] == 413
        assert call("compile", {**request, "source": "Screen S { Label { x: " + "[" * 100 + "0" + "]" * 100 + "; } }"})[0] == 413
        if mode == "contract":
            # Both HTTP workers enter together; parsing a large comment holds the backend gate.
            barrier = threading.Barrier(2)
            def overlapping(_):
                barrier.wait(timeout=10)
                return call("compile", {**request, "source": source + "\n//" + "x" * 2000000})[0]
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                outcomes = list(pool.map(overlapping, range(2)))
            assert sorted(outcomes) == [200, 503], outcomes
        fixture = {
            "readings": {"insufflation-pressure": 123},
            "statuses": {}, "fields": {},
            "signals": {"ECG_LEAD_II": {"samples": [bits(-0.5), bits(0.5)], "minimum": bits(-1), "maximum": bits(1), "strokeWidth": 2}},
            "viewports": {"ENDOSCOPE_PRIMARY": {"rows": [[bits(0), bits(1)]], "bins": 2, "minimum": bits(0), "maximum": bits(1), "lowColor": {"r": 0, "g": 0, "b": 0, "a": 255}, "highColor": {"r": 255, "g": 0, "b": 0, "a": 255}}},
            "clock": {"year": 2026, "month": 9, "day": 13, "hour": 12, "minute": 0, "second": 0, "colorToken": "Theme.Colors.Nominal"},
        }
        for binding in compiled["requiredBindings"]:
            if binding["kind"] == "StatusIndicator":
                fixture["statuses"][binding["key"]] = 0
            if binding["kind"] == "TextInput":
                fixture["fields"][binding["key"]] = {"text": "ABC123", "caret": 6}
        request.update(locale="en-US", fixture=fixture, clearColor={"r": 0, "g": 0, "b": 0, "a": 255})
        for change in ({"locale": "xx-XX"}, {"fixture": {}}, {"fixture": {**fixture, "readings": {"absent": 1}}}):
            assert call("frame", {**request, **change})[0] == 422
        invalid = copy.deepcopy(request)
        invalid["fixture"]["readings"]["insufflation-pressure"] = 1000000
        assert call("frame", invalid)[0] == 422
        invalid = copy.deepcopy(request)
        invalid["fixture"]["clock"]["day"] = 0
        assert call("frame", invalid)[0] == 422
        invalid = copy.deepcopy(request)
        invalid["fixture"]["signals"]["ECG_LEAD_II"]["samples"] = [{"bits": 0x7f800000}]
        assert call("frame", invalid)[0] == 422
        invalid_status = copy.deepcopy(request)
        status_id = next(iter(fixture["statuses"]))
        invalid_status["fixture"]["statuses"][status_id] = 999
        assert call("frame", invalid_status)[0] == 422
        field_id = next(iter(fixture["fields"]))
        invalid_field = copy.deepcopy(request)
        invalid_field["fixture"]["fields"][field_id]["text"] = "lowercase"
        assert call("frame", invalid_field)[0] == 422
        if mode == "no-device":
            status, failure = call("frame", request)
            assert status == 503 and "PRV004" in json.dumps(failure), failure
        elif mode == "pixel":
            frames = {}
            for locale in ("en-US", "fr-FR"):
                for _ in range(5):
                    status, frame = call("frame", {**request, "locale": locale})
                    assert status == 200, frame
                    png = base64.b64decode(frame["pngBase64"])
                    assert png.startswith(b"\x89PNG\r\n\x1a\n")
                    assert (frame["width"], frame["height"]) == (1280, 720)
                    assert frame["synthetic"] and frame["backend"]
                    assert locale not in frames or frames[locale] == png
                    frames[locale] = png
                    assert call("frame", invalid)[0] == 422
            assert frames["en-US"] != frames["fr-FR"]
            # Every generic dynamic binding must affect production pixels, not merely metadata.
            variants = []
            for kind in ("readings", "statuses", "fields", "clock", "signals", "viewports"):
                changed = copy.deepcopy(request)
                data = changed["fixture"][kind]
                if kind == "readings": data["insufflation-pressure"] = 456
                elif kind == "statuses": data[status_id] = 1
                elif kind == "fields": data[field_id] = {"text": "XYZ789", "caret": 0}
                elif kind == "clock": data["second"] = 1
                elif kind == "signals": data["ECG_LEAD_II"]["samples"].reverse()
                else: data["ENDOSCOPE_PRIMARY"]["highColor"] = {"r": 0, "g": 0, "b": 255, "a": 255}
                variants.append((kind, changed))
            for kind, changed in variants:
                status, changed_frame = call("frame", changed)
                assert status == 200, (kind, changed_frame)
                assert base64.b64decode(changed_frame["pngBase64"]) != frames["en-US"], kind
            empty_streams = copy.deepcopy(request)
            empty_streams["fixture"]["signals"]["ECG_LEAD_II"]["samples"] = []
            empty_streams["fixture"]["viewports"]["ENDOSCOPE_PRIMARY"]["rows"] = []
            assert call("frame", empty_streams)[0] == 200
            edited = source.replace("height: 96px", "height: 95px")
            status, frame = call("frame", {**request, "source": edited})
            assert status == 200, frame
            assert base64.b64decode(frame["pngBase64"]) != frames["en-US"]
            # Independent production path: the existing verifier reads our compiled bundle,
            # creates its own bindings/draw list/device and writes an unannotated frame.
            static_source = """Screen EndoscopeMonitor {
                layout: Vertical { spacing: 0px; padding: 0px; }
                surface: 1280px, 720px;
                Image {
                    id: logo;
                    width: 240px;
                    height: 72px;
                    source: img("brand-mark");
                }
                Label {
                    id: title;
                    width: 1000px;
                    height: 72px;
                    text: t("STR-EM-TITLE");
                    color: Theme.Colors.Nominal;
                }
            }"""
            status, static_frame = call("frame", {**request, "source": static_source, "fixture": {}})
            assert status == 200, static_frame
            bundle = root / "generated/screen/endoscope-monitor"
            bundle.mkdir(exist_ok=True)
            for key in ("package", "goldens"):
                (bundle / (key + ".json")).write_text(json.dumps(static_frame[key], sort_keys=True, indent=2, ensure_ascii=False) + "\n")
            verifier = Path(binary).with_name("mdux-verify-ui" + (".exe" if os.name == "nt" else ""))
            result = subprocess.run([str(verifier), "--screen=" + str(bundle), "--locales=all", "--frame-image-dir=" + str(work / "oracle")], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            oracle = work / "oracle/endoscope-monitor.en-US.frame.png"
            assert base64.b64decode(static_frame["pngBase64"]) == oracle.read_bytes()
        assert (root / "recipes/screen/endoscope-monitor/EndoscopeMonitor.medui").read_text() == source
        print("preview", mode, "passed")
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
