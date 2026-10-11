#!/usr/bin/env python3
"""stage-demo.py DIR — build an isolated demo world for screenshots and tests.

Creates DIR/{data,claude,conf} with a demo chat store, a fake live registry
(pids 1-4 always exist, so the sessions look alive) and one usage-limit
freeze. Nothing here touches the real ~/.claude or ~/.local/share/rezoom.

Run the app against it:
    QT_QPA_PLATFORM=offscreen REZOOM_DATA_DIR=DIR/data REZOOM_CLAUDE_DIR=DIR/claude XDG_CONFIG_HOME=DIR/conf \\
    REZOOM_SCREENSHOT=out.png dbus-run-session -- build/rezoom --resume Modal
"""
import json
import os
import sys
import time

base = sys.argv[1]
data, claude, conf = f"{base}/data", f"{base}/claude", f"{base}/conf"

for d in (data, f"{claude}/sessions", f"{claude}/projects", f"{conf}/rezoom"):
    os.makedirs(d, exist_ok=True)

now = int(time.time() * 1000)


def sid(n):
    return f"d3m0{n:04d}-0000-4000-8000-{'0' * 8}{n:04d}"


# pid, chat number, registry status, name
# (claude derives names as <folder>-<2 hex>, the handle other agents use)
for pid, n, status, name, tag in [(1, 2, "idle", "frontend", "3a"), (3, 4, "busy", "pipeline", "c7"),
                                  (4, 6, "idle", "infra", "e1")]:
    json.dump({"pid": pid, "sessionId": sid(n), "cwd": f"/home/dev/{name}", "status": status,
               "name": f"{name}-{tag}", "nameSource": "derived", "kind": "interactive",
               "updatedAt": now},
              open(f"{claude}/sessions/{pid}.json", "w"))


def chat(n, title, kind, preview, tint, cwd, ago_min, **extra):
    c = {"id": f"c{n:03d}", "title": title, "kind": kind,
         "claudeSessionId": sid(n) if kind in ("claude", "codex") else "",
         "cwd": cwd, "host": "", "entryCommand": "", "templateName": "", "commandOverride": "",
         "tmuxSession": "", "preview": preview, "tint": tint, "titleLocked": True,
         "archived": False, "createdAt": now - 3 * 86400000, "lastActiveAt": now - ago_min * 60000}
    c.update(extra)
    return c


# Chat 1's pane prints a scripted claude frame instead of running claude, so
# a hero shot (`--resume Rate-limit`) has a terminal worth looking at.
demo_cmd = f"clear; cat {base}/fake-claude.txt; sleep 3600"

chats = [
    chat(1, "Rate-limit the upload endpoint", "claude",
         "Added a token bucket per API key — 12 tests pass.", "tint-green", "/home/dev/api-server", 1,
         commandOverride=demo_cmd),
    chat(2, "Modal flickers on save", "claude",
         "Found it: the form re-mounts on every keystroke.", "tint-blue", "/home/dev/frontend", 3),
    chat(3, "Bisect the scheduler regression", "shell", "", "tint-red", "/home/dev/kernel", 8),
    chat(4, "Backfill metrics from parquet", "claude",
         "Streaming 2.1M rows, batch 40 of 212", "tint-purple", "/home/dev/pipeline", 12),
    chat(5, "homelab", "ssh", "ssh homelab", "tint-orange", "", 95,
         entryCommand="ssh homelab", host="homelab"),
    chat(6, "Terraform drift in staging", "codex",
         "Plan shows 3 changes; none destructive.", "tint-neutral", "/home/dev/infra", 140),
    chat(7, "tmux: nightly builds", "tmux", "tmux attach -t builds", "tint-blue", "", 600,
         tmuxSession="builds"),
    chat(8, "Draft: what a pty actually is", "claude",
         "Outline done — 5 sections, ~1,800 words.", "tint-green", "/home/dev/blog", 1500),
]
json.dump({"chats": chats}, open(f"{data}/sessions.json", "w"), indent=1)

open(f"{data}/notifications.jsonl", "w").write(json.dumps(
    {"ts": now, "session_id": sid(6), "message": "You've reached your usage limit. Resets at 5pm.",
     "title": ""}) + "\n")

E = "\033["
g, d, b, r, o, c = E + "32m", E + "2m", E + "1m", E + "0m", E + "38;5;173m", E + "36m"
open(f"{base}/fake-claude.txt", "w").write(f"""{d}~/dev/api-server{r} {g}❯{r} claude --resume d3m00001

{o}●{r} Reading {b}src/routes/upload.ts{r} and {b}src/middleware/{r}

{o}●{r} The upload route has no limiter, so one API key can saturate
  the ingest queue. I'll add a token bucket keyed by API key:

  {c}src/middleware/rateLimit.ts{r}   {g}+48{r}
  {c}src/routes/upload.ts{r}          {g}+3{r}  {E}31m-1{r}
  {c}test/rateLimit.test.ts{r}        {g}+71{r}

{o}●{r} Ran {b}npm test -- rateLimit{r}
  {g}✓{r} allows a burst up to the bucket size
  {g}✓{r} refills at 5 tokens/s
  {g}✓{r} returns 429 with Retry-After when empty
  {d}… 9 more{r}

{o}●{r} Done. Uploads are capped at 10/s per key with a burst of 20;
  over-limit requests get {b}429{r} and a {b}Retry-After{r} header.

{d}────────────────────────────────────────────────────────────{r}
{g}❯{r} {d}_{r}
""")

# "Modal flickers" finished while you were elsewhere.
open(f"{conf}/rezoom/rezoom.conf", "w").write("[ui]\nunread=c002\n")
print(f"demo staged in {base}")
