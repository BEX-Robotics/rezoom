# Rezoom — contributor conventions

C++ style follows the BEX conventions (KNR braces, no braces on
single-statement bodies, trailing acronyms capitalized (`chatID`, `shellPID`),
strict blank-line rules, functions ≤60 lines, `//` why-comments, hex escapes
with decoding comments for non-ASCII literals). If you have the bex monorepo,
`scripts/check-cpp-style.py` from there is the mechanical checker — its
zero-init warnings on Qt types are exempt false positives.

Git: component-prefixed subjects (`core:`, `gui:`, `cli:`, `dist:`), one
reviewable change per commit, amend unpushed fixes instead of stacking.

## UI policy

- **Any text showing an id, command, path or number must be mouse-copyable.**
  QLabel → `setTextInteractionFlags(Qt::TextSelectableByMouse)`. Qt's static
  convenience dialogs can't do it — use `getTextSelectable()` /
  `askSelectable()` in `gui/mainwindow.cpp` instead of
  `QInputDialog::getText` / `QMessageBox::question` whenever the text carries
  anything copy-worthy.
- Keyboard: Rezoom owns `Ctrl+Shift` chords (and Ctrl+PgUp/PgDn) only — plain
  Ctrl/Alt keys must keep reaching the shells inside embedded terminals.
  Every new feature button/menu gets a chord, the surface displays it
  (menu text, tooltip), and `gui/shortcutsdialog.cpp` lists it.
- **Features that can't work on this machine disappear.** Detect the
  prerequisite (tool installed, DBus/KWin present, process in a Konsole,
  hook script shipped) and hide the menu item/button, instead of showing
  something that fails or silently does nothing. Settings may still show
  the feature with its readiness and a fix, but never an action that
  can't run.
- Strings with non-ASCII glyphs: `QString::fromUtf8("\xe2...")` or `tr()` —
  never `QStringLiteral` with hex escapes (it widens bytes as Latin-1).

## Ground rules

- The app only ever *reads* `~/.claude/` (registry, transcripts). Never write
  there except the documented Notification-hook merge into settings.json.
  The one write to `~/.claude.json` (trust pre-accept) is opt-in, default off.
- Never connect to a remote host without an explicit user click.
- `SessionStore` mutations go through `mutate()` — decisions that must not
  race (dedup-then-add) happen inside the locked op, not before it.
- `$REZOOM_DATA_DIR` / `$REZOOM_CLAUDE_DIR` for tests and demos; never test
  against the user's real store.
