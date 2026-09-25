#pragma once
#include <QHash>
#include <QString>

// External Konsole window titles by konsole pid, read over DBus. Konsole
// keeps its title on claude's live activity line — the only name the user's
// eyes have actually seen for an external session. Empty without a bus.
namespace KonsoleTitles {

QHash<int, QString> byKonsolePid();

// "✳ BIT architecture review" → "BIT architecture review".
QString stripStatusGlyph(QString title);
}
