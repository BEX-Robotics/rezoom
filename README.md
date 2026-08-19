# Rezoom

WhatsApp-style organizer for Claude Code sessions and terminals. Native C++/Qt6,
KDE-native dark/light theming, embedded Konsole terminals. Every session —
claude, plain shell, ssh, tmux — is a "chat" in the left panel and stays
**forever resumable**.

```
┌──────────────┬──────────────────────────┐
│ ● bex-29     │ pavel@host bex %         │
│ ● callqueue  │ > claude --resume 489d.. │
│ ○ pavel-7a   │ ╭─ Claude Code ────────╮ │
│ ○ kalibr ssh │ │ ...session output... │ │
│ ─ archived ▸ │ ╰──────────────────────╯ │
│ [+ new]      │ >                        │
└──────────────┴──────────────────────────┘
```

## What it does

- **Live presence**: watches `~/.claude/sessions/` (written by Claude Code
  itself) — green dot = claude working, amber = waiting for you, blue = shell.
  A chat that finishes work while unfocused gets an unread marker.
- **Automatic session tracking**: sessions started inside Rezoom record their
  claude session id automatically (including id changes after `/clear`), plus
  ssh/tmux commands typed into embedded terminals ("how we entered").
- **Resume**: dead chats show the exact resumable command and a big ▶ Rezoom
  button. Command templates are configurable per chat and globally
  (`~/.config/rezoom/rezoom.conf`).
- **Adopt**: suck in external sessions — running claudes, any historical
  transcript from `~/.claude/projects/`, tmux sessions, live ssh clients.
- **Floating groups**: pop chats out into floating tab windows (e.g. one per
  screen), pull them back in; layout persists across restarts.
- **Consent-first SSH**: ssh chats never connect on their own — the button
  press is the consent. Optional remote scan finds claude/tmux sessions on a
  host you already connect to.
- **Pull in live**: an externally running session can be pulled into an
  embedded pane without killing it (reptyr); if ptrace is restricted the app
  verifies the failure and falls back to lossless kill-and-resume.
- **Archive**: hide finished chats without losing resumability.

## Build

Linux:

```sh
sudo apt install qt6-base-dev libkf6parts-dev libkf6service-dev \
                 libkf6coreaddons-dev libkf6i18n-dev extra-cmake-modules
cmake -B build -G Ninja
ninja -C build
build/rezoom
```

Runtime needs `konsole-kpart` (the embeddable Konsole component) and benefits
from `zsh` and `tmux`.

macOS:

```sh
brew install cmake ninja qt
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix)"
ninja -C build
build/rezoom
```

KF6/Konsole is KDE-only, so macOS builds have no embedded terminals: opening a
chat offers to launch it in a Terminal.app window instead (same for
`rezoom-cli resume`). Presence tracking, adopt, archive and the rest work the
same.

## Spread to other machines

Debian/Ubuntu:

```sh
scripts/make-deb.sh
sudo apt install ./dist/rezoom_<version>_amd64.deb   # pulls Qt/KF6 deps via apt
```

Fedora/openSUSE: `rpmbuild -ba dist/rezoom.spec` (or `osc`/copr with the same
spec). Arch: `dist/PKGBUILD` is AUR-ready (`makepkg -si` from a dir containing
it). All recipes depend on distro packages — nothing is bundled. Flatpak and
AppImage are deliberately absent: they bundle runtimes, and Rezoom leans on the
host's Konsole/KF6 instead.

## CLI

```sh
rezoom-cli list                  # chats + live presence (TSV)
rezoom-cli resume <query>        # reopen a chat in a Konsole window
rezoom-cli resume <query> --print
rezoom-cli adopt-running         # adopt every untracked running claude
```

## Files

| Path | What |
|---|---|
| `~/.local/share/rezoom/sessions.json` | chat records |
| `~/.config/rezoom/rezoom.conf` | templates, prefs, UI layout |
| `~/.claude/sessions/<pid>.json` | live registry (written by claude, read-only) |
| `~/.claude/projects/*/<uuid>.jsonl` | transcripts (read-only) |
