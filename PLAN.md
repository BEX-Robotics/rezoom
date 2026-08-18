# Rezoom — WhatsApp-style organizer for Claude sessions & terminals

## Context

Pavel runs ~25 concurrent Claude Code sessions in separate tinted Konsole windows, currently
managed by hand-rolled scripts (`claude-freeze`/`claude-restore`/`claude-sessions`,
`raise-konsole.sh`). He wants a single native app — chat list on the left, terminals on the
right — where every session is *forever resumable*, external sessions can be adopted, and
old ones archived.

**Key discovery that shapes the design:** Claude Code ≥ 2.1.220 maintains
`~/.claude/sessions/<pid>.json` for every running session: `{pid, sessionId, cwd, status
(idle|busy|shell), name, updatedAt}`. Watching that directory gives *exact* live tracking and
WhatsApp-style presence — no memory scanning, no hooks. Dead sessions live in
`~/.claude/projects/<munged-cwd>/<uuid>.jsonl` (first-user-message preview extractable, as
`claude-freeze` already does).

**Decisions made with user:**
- Name: **Rezoom** (binary `rezoom`), repo stays at `~/dev/claudsap`
- Terminals: **embedded Konsole KPart** (`konsolepart.so` for KF6 is already installed — same
  engine as Konsole: user's profiles, ZSH) + pop-out to a real Konsole window
- GUI first (Qt6 Widgets — follows KDE Breeze dark/light automatically; FLTK 1.3 on this
  system has no terminal widget and no system theming). Shared core lib → thin CLI ships in
  v1; full TUI is a possible later phase.
- SSH: auto-*detect* (local `ssh` processes + what runs inside our embedded shells), record
  "how we entered", but **never connect anywhere without an explicit user click**. No
  password storage.

## Dependencies (one-time apt install, needs sudo)

`qt6-base-dev libkf6parts-dev libkf6service-dev libkf6coreaddons-dev libkf6i18n-dev
extra-cmake-modules` (all have candidates in the enabled repos; g++/cmake 4.2/ninja already
present; `konsole-kpart` runtime already installed).

## Architecture

```
claudsap/
  CMakeLists.txt            # C++20, Qt6 Widgets, KF6 Parts/Service/CoreAddons
  core/                     # static lib, QtCore only — GUI-independent
    chat.h                  # Chat record struct + (de)serialization
    sessionstore.{h,cpp}    # persistence: ~/.local/share/rezoom/sessions.json (atomic writes)
    templates.{h,cpp}       # resumable-command templates, ~/.config/rezoom/rezoom.conf
    liveregistry.{h,cpp}    # QFileSystemWatcher on ~/.claude/sessions/ + pid liveness
    transcriptindex.{h,cpp} # scan ~/.claude/projects/*/*.jsonl → adoption candidates
    processscout.{h,cpp}    # /proc walk: ssh/claude/tmux under a shell pid; tmux ls
  gui/
    main.cpp
    mainwindow.{h,cpp}      # QSplitter: chat list | QStackedWidget of panes
    chatlistmodel.{h,cpp}   # QAbstractListModel over SessionStore + LiveRegistry
    chatdelegate.{h,cpp}    # WhatsApp-style row: tint avatar, title, preview, presence, time
    terminalpane.{h,cpp}    # hosts one konsolepart KPart via TerminalInterface
    resumepane.{h,cpp}      # placeholder for non-running chat: shows command + [Rezoom] button
    adoptdialog.{h,cpp}     # tabs: Running / History / Terminals (tmux+ssh)
    settingsdialog.{h,cpp}  # template editor + prefs
    chatview.{h,cpp}        # per-chat container: resume pane or terminal, movable
    floatwindow.{h,cpp}     # floating group window (tabs), e.g. one per screen
  cli/
    main.cpp                # rezoom-cli: list / resume / adopt-running (links core only)
```

### Data model (`~/.local/share/rezoom/sessions.json`)

```json
{ "chats": [ {
  "id": "app-uuid", "title": "camera/kalibr session", "kind": "claude|shell|ssh|tmux",
  "claudeSessionId": "489d98ba-…", "cwd": "/home/pavel/dev/bex", "host": null,
  "resumeCommand": "claude --resume {session_id}", "template": "claude-resume",
  "preview": "You're the camera/kalibr session — load…", "tint": "tint-green",
  "archived": false, "createdAt": "…", "lastActiveAt": "…" } ] }
```

`resumeCommand` stores the template reference; a per-chat override field holds an edited raw
command. Placeholders: `{session_id} {cwd} {host} {tmux_session}`.

### Default templates (`~/.config/rezoom/rezoom.conf` [templates], editable in Settings + by hand)

- `claude-resume`: `claude --resume {session_id}` (run in `{cwd}`)
- `ssh-claude`: `ssh -t {host} 'cd {cwd} && claude --resume {session_id}'`
- `ssh-plain`: re-run recorded entry command (e.g. `ssh dev.example.com`)
- `tmux-attach`: `tmux attach -t {tmux_session}`
- `shell`: plain zsh in `{cwd}`; optional "persistent" variant `tmux new -A -s rezoom-{name}`

### Live tracking & presence

`LiveRegistry` watches `~/.claude/sessions/`, prunes entries with dead pids, and emits
per-sessionId presence: **green** = busy (claude working), **grey** = idle (waiting for you),
**blue** = shell, **hollow** = not running (resumable). A busy→idle transition on an
unfocused chat sets an unread-style highlight badge. When a resumed process registers a *new*
sessionId under the same pid (e.g. after `/clear`), the chat record's `claudeSessionId` is
updated automatically — this is the "automatically save session IDs" requirement.

### Terminal panes

- Load `konsolepart` via `KPluginFactory` (`kf6/parts/konsolepart.so` — confirmed present),
  drive it through `TerminalInterface` (`<kde_terminal_interface.h>` ships in libkf6parts-dev): `showShellInDir(cwd)` then `sendInput("claude
  --resume <id>\n")`. When claude exits, the user lands in zsh in that cwd (chat stays open).
- `terminalProcessId()` gives the shell pid → `ProcessScout` watches its children: a `claude`
  child pid keys into `~/.claude/sessions/<pid>.json` for exact sid binding; an `ssh` child's
  cmdline is recorded as that chat's "how we entered" (auto-save for SSH); a `tmux` child
  records the attach target. So sessions started *inside* any Rezoom terminal are adopted
  automatically.
- **Pop out**: launches the user's tinted `~/.local/bin/konsole` wrapper with the resume
  command, closes the embedded pane; LiveRegistry keeps tracking it externally.
- If a chat's sid is already running *outside* the app, the Rezoom button is replaced by
  "Raise window" (reuse the DBus technique from `~/.claude/raise-konsole.sh`, invoked with
  the claude pid) — prevents double-resume.
- App close with live embedded sessions → confirm dialog (they're all resumable anyway).

### Adoption ("suck in" external sessions)

Adopt dialog, three tabs — all local reads, zero network:
1. **Running** — LiveRegistry entries not yet tracked (all ~25 current sessions adoptable in
   one click each or "adopt all"); title from the registry `name`, preview from transcript.
2. **History** — TranscriptIndex over `~/.claude/projects/*/*.jsonl`: preview (first user
   message, `claude-freeze`-style single-grep), cwd, mtime; searchable.
3. **Terminals** — `tmux ls` sessions + running local `ssh` client processes (cmdline → host,
   recorded as the resumable entry command).
Plus one-time best-effort import of `~/.claude/session-snapshot.tsv` on first run.
Existing freeze/restore scripts are left untouched.

### SSH (consent-first)

Detection is passive (reading /proc and tmux — never connecting). Resuming an SSH chat shows
the exact command on the Rezoom button ("Connect: ssh dev.example.com …") — the click *is*
the consent; nothing external is ever launched automatically. Optional per-chat "Scan remote"
button (explicit click) runs `ssh <entry> 'cat ~/.claude/sessions/*.json 2>/dev/null; tmux
ls 2>/dev/null'` to upgrade a plain SSH chat into remote-claude/tmux chats.

### CLI (`rezoom-cli`)

`list` (chats + presence, TSV), `resume <query>` (prints or spawns via konsole wrapper),
`adopt <sid|--all-running>`. Links core only. Full FTXUI TUI = future phase, enabled by the
core/GUI split.

## Implementation order

1. **Deps + skeleton**: apt install, CMake project, empty window builds & runs.
2. **Core**: `Chat`/`SessionStore` (atomic JSON), `Templates`, `LiveRegistry`,
   `TranscriptIndex`, `ProcessScout` — each with a tiny standalone test in `cli` scaffolding.
3. **GUI shell**: MainWindow splitter, ChatListModel+delegate (presence dots, search filter,
   Active/Resumable/Archived sections), ResumePane.
4. **Terminals**: TerminalPane with konsolepart (vendor `kde_terminal_interface.h` if the
   header isn't shipped), launch/resume flow, child-process tracking, sid auto-update,
   pop-out, raise-external.
5. **Adoption**: AdoptDialog (3 tabs) + snapshot.tsv import + auto-adopt inside own panes.
6. **Archive/rename/settings**: context menu, template editor, close-confirm.
7. **CLI** + `.desktop` file + install target (`~/.local/bin/rezoom`, icon).

## Verification

- `cmake -B build -G Ninja && ninja -C build` clean.
- Launch against real data (read-only until adopt): presence dots must match `pgrep`-visible
  claude pids and their `status` fields; toggle a session busy/idle by giving it work.
- Adopt one dead transcript from History → Rezoom button → embedded terminal resumes it →
  chat flips to running with correct sid binding; type `/clear` → verify record's sid updates.
- Adopt-running on an external Konsole session → "Raise window" focuses it.
- Start `ssh` inside an embedded pane → chat records entry command; kill pane → Rezoom button
  offers reconnect (and does nothing until clicked).
- Archive/unarchive; edit a template in Settings and confirm regenerated command; restart app
  → state persists; `rezoom-cli list` matches GUI.
- Dark/light: flip KDE color scheme, app follows without restart.

## Post-plan additions (implemented)

- **Floating groups**: any chat can move to a floating tab window (one per
  screen), pull back in, or hop between groups; window set + geometry + splitter
  + assignments persist in rezoom.conf and are restored on start.
- **`rezoom --resume <query>`**: select + launch a chat straight from the
  command line (KRunner-friendly).
- Config is a plain INI conf (`rezoom.conf`), not JSON; `sessions.json` stays
  JSON (machine-managed data).
