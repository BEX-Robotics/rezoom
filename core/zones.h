#pragma once
#include <QList>
#include <QString>

// Claude account zones: separate Claude Code config dirs (CLAUDE_CONFIG_DIR),
// each with its own login, sessions and transcripts. Opt-in only — with no
// zones added there is exactly one, the default ~/.claude, and every code
// path behaves as before multi-account support existed.
namespace Zones {

struct Zone {
    QString name; // empty = the default zone
    QString dir;
};

// ~/.claude, or $REZOOM_CLAUDE_DIR for tests and demos.
QString defaultDir();

// Default zone first, then user-added ones (rezoom.conf [zones]).
QList<Zone> all();

bool any(); // any zone beyond the default?

// Config dir for a zone name; the default dir for "" or unknown names.
QString dirFor(const QString &name);

// Create ~/.claude-<name> and register it. shareSetup symlinks the default
// zone's settings.json, CLAUDE.md, rules, commands, agents and skills into
// it — sessions, transcripts and the login stay separate.
bool add(const QString &name, bool shareSetup, QString *error);

// "CLAUDE_CONFIG_DIR='<dir>' " for non-default zones, else "".
QString envPrefix(const QString &name);
}
