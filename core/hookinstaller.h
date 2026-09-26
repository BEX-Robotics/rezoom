#pragma once
#include <QString>

// Installs/removes rezoom-notify-hook as a Claude Code Notification hook in
// ~/.claude/settings.json (backup at settings.json.bak-rezoom first). This
// is the one documented write into ~/.claude/.
namespace HookInstaller {

// Absolute path of the hook script, empty when it isn't installed on PATH.
QString hookPath();

bool installed();
bool install(QString *error);
bool uninstall(QString *error);
}
