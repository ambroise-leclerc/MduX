#!/usr/bin/env python3
"""Exercise Studio documents and reviewable change proposals against the real service and real git.

No GPU is needed: every request here compiles, and none renders. The served checkout, the remote and
the service's own git directory are three separate repositories in a temporary directory, so the test
can prove the service never touches the checkout it serves.
"""
import copy
import http.client
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile

binary, repo = sys.argv[1:3]
recipe = "recipes/screen/endoscope-monitor.toml"
source_path = "recipes/screen/endoscope-monitor/EndoscopeMonitor.medui"
git_env = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=os.devnull)


def git(*args, cwd=None, check=True):
    result = subprocess.run(["git", *args], cwd=cwd, env=git_env, capture_output=True, text=True, timeout=60)
    if check:
        assert result.returncode == 0, (args, result.stdout, result.stderr)
    return result.stdout.strip()


def identity(*prefix):
    git(*prefix, "config", "user.name", "MduX Preview Test")
    git(*prefix, "config", "user.email", "preview-test@localhost")


def findings(body):
    body = body or {}
    return (body.get("diagnostics") or {}).get("findings") or body.get("findings") or []


def codes(body):
    return {f["code"] for f in findings(body)}


def node(document, wanted):
    for item in document["nodes"]:
        for candidate in [item, *item["children"]]:
            if any(f["name"] == "id" and f["value"]["text"] == wanted for f in candidate["fields"]):
                return candidate
    raise AssertionError(wanted)


def field(document, node_id, name):
    return next(f for f in node(document, node_id)["fields"] if f["name"] == name)


with tempfile.TemporaryDirectory(prefix="mdux-proposal-test-") as work:
    work = Path(work)
    token = secrets.token_hex(32)
    (work / "token").write_text(token)

    # The served checkout: a real repository, so its refs, index and status can be compared later.
    root = work / "repo"
    shutil.copytree(Path(repo) / "recipes/screen", root / "recipes/screen")
    shutil.copytree(Path(repo) / "generated", root / "generated")
    broken = root / "recipes/screen/broken.toml"
    broken.write_text((root / recipe).read_text().replace(source_path, "recipes/screen/broken.medui"))
    (root / "recipes/screen/broken.medui").write_text("Screen Broken {\n    Label { width: 1rem; }\n}\n")
    git("init", "--quiet", "--initial-branch=develop", cwd=root)
    identity("-C", str(root))
    git("add", "--all", cwd=root)
    git("commit", "--quiet", "-m", "base", cwd=root)

    remote = work / "remote.git"
    git("init", "--quiet", "--bare", str(remote))
    git("push", "--quiet", str(remote), "develop:refs/heads/develop", cwd=root)

    service_git = work / "proposals.git"
    git("init", "--quiet", "--bare", str(service_git))
    identity("--git-dir", str(service_git))
    git("--git-dir", str(service_git), "remote", "add", "origin", str(remote))
    unreachable_git = work / "unreachable.git"
    git("init", "--quiet", "--bare", str(unreachable_git))
    identity("--git-dir", str(unreachable_git))
    git("--git-dir", str(unreachable_git), "remote", "add", "origin", str(work / "absent.git"))

    def checkout_state():
        return (git("rev-parse", "HEAD", cwd=root), git("status", "--porcelain", "--untracked-files=all", cwd=root),
                git("for-each-ref", cwd=root), (root / ".git/index").read_bytes(), (root / source_path).read_bytes())

    before = checkout_state()

    # Start-up refuses a git directory that could reach the served checkout, and orphan options.
    for rejected in (["--proposal-git-dir", str(root / ".git")], ["--proposal-pull-requests"], ["--proposal-remote", "upstream"]):
        result = subprocess.run([binary, "--root", str(root), "--token-file", str(work / "token"), *rejected],
                                capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and ("outside" in result.stderr or "require" in result.stderr), (rejected, result.stderr)

    servers = []

    def start(*options):
        process = subprocess.Popen([binary, "--root", str(root), "--token-file", str(work / "token"), *options],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        servers.append(process)
        address = process.stdout.readline().strip()
        assert address.startswith("http://127.0.0.1:"), (address, process.poll())
        return int(address.rsplit(":", 1)[1])

    try:
        readonly = start()
        writer = start("--proposal-git-dir", str(service_git))
        broken_remote = start("--proposal-git-dir", str(unreachable_git))
        pull_requests = start("--proposal-git-dir", str(service_git), "--proposal-pull-requests")

        def call(port, route, body=None):
            connection = http.client.HTTPConnection("127.0.0.1", port, timeout=120)
            connection.request("POST" if body is not None else "GET", "/api/" + route,
                               json.dumps(body) if body is not None else None,
                               {"Authorization": "Bearer " + token, "Content-Type": "application/json"})
            response = connection.getresponse()
            raw = response.read()
            connection.close()
            return response.status, json.loads(raw) if raw else None

        status, catalog = call(readonly, "catalog")
        assert status == 200 and catalog["documentSchemaVersion"] == 1 and catalog["proposals"] == {"enabled": False, "pullRequests": False}
        assert call(writer, "catalog")[1]["proposals"] == {"enabled": True, "pullRequests": False}
        assert {"PRV008", "PRV009", "PRV010", "PRV011", "PRV012"} <= {d["code"] for d in catalog["previewDiagnostics"]}

        # Documents: the committed source, refusal of a source that does not parse cleanly.
        status, loaded = call(readonly, "document", {"schemaVersion": 1, "recipe": recipe})
        assert status == 200 and loaded["commentLines"] is True and loaded["documentSchemaVersion"] == 1, loaded
        document = loaded["document"]
        digest = loaded["sourceDigest"]
        status, refused = call(readonly, "document", {"schemaVersion": 1, "recipe": "recipes/screen/broken.toml"})
        assert status == 422 and "document" not in refused and findings(refused), refused

        # Compile accepts a document instead of source text and returns the canonical text it compiled.
        status, compiled = call(readonly, "compile", {"schemaVersion": 1, "recipe": recipe, "document": document})
        assert status == 200 and compiled["source"].startswith("Screen EndoscopeMonitor {") and "//" not in compiled["source"], compiled
        assert call(readonly, "compile", {"schemaVersion": 1, "recipe": recipe, "document": document, "source": "x"})[0] == 400
        assert call(readonly, "document", {"schemaVersion": 1, "recipe": recipe, "document": document})[0] == 422
        injected = copy.deepcopy(document)
        field(injected, "ecg-lead-ii", "color")["name"] = "color: Theme.Colors.Alert; requirement"
        status, body = call(readonly, "compile", {"schemaVersion": 1, "recipe": recipe, "document": injected})
        assert status == 422 and "PRV002" in codes(body) and "identifier" in json.dumps(body), body
        wrong_version = copy.deepcopy(document)
        wrong_version["schemaVersion"] = 2
        assert "PRV002" in codes(call(readonly, "compile", {"schemaVersion": 1, "recipe": recipe, "document": wrong_version})[1])

        # An invalid edit returns compiler diagnostics against the canonical source, never a proposal.
        invalid = copy.deepcopy(document)
        field(invalid, "ecg-lead-ii", "color")["value"]["text"] = "Theme.Colors.Undefined"
        status, body = call(readonly, "compile", {"schemaVersion": 1, "recipe": recipe, "document": invalid})
        assert status == 422 and any(c.startswith("MEDUI-E") for c in codes(body)) and "source" in body, body

        edited = copy.deepcopy(document)
        field(edited, "ecg-lead-ii", "color")["value"]["text"] = "Theme.Colors.Alert"
        proposal = {"schemaVersion": 1, "recipe": recipe, "document": edited, "baseSourceDigest": digest, "issue": 327,
                    "slug": "studio-edit", "base": "develop", "title": "Tint the ECG trace", "description": "Studio test.",
                    "acknowledgeCommentLoss": False, "acknowledgeSafetyChanges": False, "dryRun": True}

        status, review = call(readonly, "proposals", proposal)
        assert status == 200 and review["writesEnabled"] is False and review["commentLoss"] is True, review
        assert review["safetyChanges"] == [] and review["branchPrefix"] == "327-studio-edit", review
        status, body = call(readonly, "proposals", {**proposal, "dryRun": False, "acknowledgeCommentLoss": True})
        assert status == 403 and "PRV008" in codes(body), body

        status, body = call(writer, "proposals", {**proposal, "document": invalid})
        assert status == 422 and any(c.startswith("MEDUI-E") for c in codes(body)), body
        status, body = call(writer, "proposals", {**proposal, "document": document})
        assert status == 422 and "no change" in json.dumps(body), body
        for change in ({"issue": 0}, {"slug": "Bad Slug"}, {"slug": "a--b"}, {"base": "main"}, {"base": "12-"},
                       {"title": ""}, {"title": "two\nlines"}, {"dryRun": "yes"}):
            status, body = call(writer, "proposals", {**proposal, **change})
            assert status == 422 and "PRV002" in codes(body), (change, status, body)
        assert call(writer, "proposals", {**proposal, "base": "12-stacked-predecessor"})[0] == 200
        status, body = call(writer, "proposals", {**proposal, "unexpected": True})
        assert status == 422, body

        # Optimistic concurrency against the file that was loaded.
        status, body = call(writer, "proposals", {**proposal, "baseSourceDigest": "0" * 64, "dryRun": False})
        assert status == 409 and "PRV009" in codes(body), body

        # Explicit acknowledgements: comment loss, then safety metadata.
        status, body = call(writer, "proposals", {**proposal, "dryRun": False})
        assert status == 409 and "PRV010" in codes(body), body
        traced = copy.deepcopy(edited)
        field(traced, "emergency-halt", "requirement")["value"]["text"] = "REQ-EM-009"
        status, review = call(writer, "proposals", {**proposal, "document": traced})
        assert status == 200 and [(c["nodeId"], c["change"]) for c in review["safetyChanges"]] == [("emergency-halt", "changed")], review
        assert review["safetyChanges"][0]["before"]["requirement"]["text"] == "REQ-EM-003"
        status, body = call(writer, "proposals", {**proposal, "document": traced, "dryRun": False, "acknowledgeCommentLoss": True})
        assert status == 409 and "PRV011" in codes(body), body

        acknowledged = {**proposal, "dryRun": False, "acknowledgeCommentLoss": True}

        # Failed git operations leave nothing behind and say what failed.
        status, body = call(broken_remote, "proposals", acknowledged)
        assert status == 502 and "PRV012" in codes(body) and "fetch" in json.dumps(body), body
        hook = remote / "hooks/pre-receive"
        hook.write_text("#!/bin/sh\necho refused by policy >&2\nexit 1\n")
        hook.chmod(0o755)
        try:
            status, body = call(writer, "proposals", acknowledged)
            assert status == 502 and "PRV012" in codes(body) and "push" in json.dumps(body), body
        finally:
            hook.unlink()
        assert git("--git-dir", str(remote), "for-each-ref", "refs/heads/327-*") == ""

        # A successful proposal: one new branch, one changed file, the base as parent, nothing merged.
        base_commit = git("--git-dir", str(remote), "rev-parse", "develop")
        status, created = call(writer, "proposals", acknowledged)
        assert status == 201, created
        assert created["branch"].startswith("327-studio-edit-") and created["baseCommit"] == base_commit, created
        assert created["pullRequestUrl"] is None and created["warning"] is None, created
        assert git("--git-dir", str(remote), "rev-parse", "refs/heads/" + created["branch"]) == created["commit"]
        assert git("--git-dir", str(remote), "rev-parse", created["commit"] + "^") == base_commit
        assert git("--git-dir", str(remote), "diff", "--name-only", base_commit, created["commit"]) == source_path
        committed = subprocess.run(["git", "--git-dir", str(remote), "show", f"{created['commit']}:{source_path}"],
                                   env=git_env, capture_output=True, timeout=60).stdout.decode()
        assert committed == review["source"] or committed == call(writer, "proposals", proposal)[1]["source"], committed
        message = git("--git-dir", str(remote), "log", "-1", "--format=%B", created["commit"])
        assert "Theme.Colors.Alert" in committed and "#327" in message and "Safety metadata changes: none" in message, message
        assert git("--git-dir", str(remote), "rev-parse", "develop") == base_commit, "the service must never merge"

        # Safety metadata changes go through once acknowledged.
        status, body = call(writer, "proposals", {**acknowledged, "document": traced, "acknowledgeSafetyChanges": True})
        assert status == 201, body
        message = git("--git-dir", str(remote), "log", "-1", "--format=%B", body["commit"])
        assert "Safety metadata changes: 1 (acknowledged)" in message, message

        # Pull requests are optional: a non-GitHub remote is a warning, not a failure.
        tinted = copy.deepcopy(document)
        field(tinted, "ecg-lead-ii", "color")["value"]["text"] = "Theme.Colors.Fault"
        status, body = call(pull_requests, "proposals", {**acknowledged, "document": tinted})
        assert status == 201 and body["pullRequestUrl"] is None and "not a GitHub repository" in body["warning"], body

        # A base that moved after the source was loaded is stale, even though the disk still matches.
        clone = work / "upstream"
        git("clone", "--quiet", "--branch", "develop", str(remote), str(clone))
        identity("-C", str(clone))
        (clone / source_path).write_text((clone / source_path).read_text() + "\n")
        git("commit", "--quiet", "-am", "upstream change", cwd=clone)
        git("push", "--quiet", "origin", "develop", cwd=clone)
        status, body = call(writer, "proposals", acknowledged)
        assert status == 409 and "PRV009" in codes(body) and "develop" in json.dumps(body), body

        assert checkout_state() == before, "the served checkout's HEAD, refs, index, status or source changed"
        print("preview proposals passed")
    finally:
        for process in servers:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
