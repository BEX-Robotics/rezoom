#pragma once
#include <QString>

// Open (cwd, command) in a new external terminal window: Konsole on Linux
// (whichever `konsole` is first on PATH, so user wrappers win), Terminal.app
// on macOS.
namespace ExternalTerminal {

void launch(const QString &cwd, const QString &command);

// The user's login shell ($SHELL), /bin/sh when unset or missing. Commands
// run through it interactively (-ic) so the user's aliases work.
QString userShell();

} // namespace ExternalTerminal
