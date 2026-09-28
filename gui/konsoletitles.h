#pragma once
#include <QHash>
#include <QString>

// External Konsole window titles by konsole pid, read over DBus. Konsole
// keeps its title on claude's live activity line — the only name the user's
// eyes have actually seen for an external session. Empty without a bus.
namespace KonsoleTitles {

QHash<int, QString> byKonsolePid();

// Terminal sessions (tabs/splits) in one konsole process; -1 = unknown.
int sessionCount(int konsolePid);

// What a Konsole title says about a claude inside it (also over ssh —
// claude sets the title remotely): "busy" (◐◓◑◒ spinner), "idle" (✳), or
// "" when the title doesn't look like claude at all.
QString claudeStateFromTitle(const QString &title);

// "✳ BIT architecture review" → "BIT architecture review".
QString stripStatusGlyph(QString title);
}
