#pragma once
#include <QString>

// Open (cwd, command) in a new external terminal window: Konsole on Linux
// (honoring the user's ~/.local/bin/konsole tinted-profile wrapper),
// Terminal.app on macOS.
namespace ExternalTerminal {

void launch(const QString &cwd, const QString &command);

} // namespace ExternalTerminal
