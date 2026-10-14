#pragma once
#include <optional>

#include <QHash>
#include <QObject>
#include <QTimer>

#include "chat.h"
#include "zones.h"

class QFileSystemWatcher;

// One running Claude Code session, as reported by claude itself in
// ~/.claude/sessions/<pid>.json.
struct LiveEntry {
    int pid = 0;
    QString sessionID;
    QString status; // "idle" | "busy" | "shell"
    QString kind;   // "interactive" for real CLI sessions, "bg" for background ones
    QString jobID;  // background sessions: the id `claude attach` takes
    QString name;
    QString nameSource; // "derived" = machine-made, prefer transcript titles
    QString cwd;
    QString zone; // account zone name, "" = default ~/.claude
    qint64 updatedAt = 0;
};

// Watches ~/.claude/sessions/ — the exact live-session registry claude
// maintains (pid, sessionId, cwd, live status). Entries with dead pids
// are ignored and pruned on the fly.
// Worth turning into a chat automatically? Interactive, not living in a
// scratch dir (/tmp claude = tooling like commit reviewers, never a
// conversation), and not spawned by another claude.
bool autoAdoptable(const LiveEntry &e);

// A conversation sent to the background goes on under a new session id, and
// the old transcript ends with a "continued-in" record naming it. For such a
// background entry, re-point the chat that held the old id; true = done.
bool followContinuation(QList<Chat> &list, const LiveEntry &e);

// "claude attach <id>" for a background entry (it runs on claude's own pty
// host: attaching is how it comes into a terminal), "" for any other.
QString attachCommand(const LiveEntry &e);

class LiveRegistry : public QObject {
    Q_OBJECT
public:
    explicit LiveRegistry(QObject *parent = 0);

    const QHash<QString, LiveEntry> &bySessionID() const { return entries; }

    std::optional<LiveEntry> entryForSession(const QString &sessionID) const;

    // A session id can be claimed by several live pids (double-resume,
    // leftovers) — everything that needs one specific process resolves
    // through the full list, not the deduped map.
    QList<LiveEntry> entriesForSession(const QString &sessionID) const;
    std::optional<LiveEntry> entryForPID(int pid) const; // falls back to reading the file

    static std::optional<LiveEntry> readPidFile(int pid);
    static bool pidAlive(int pid);

public slots:
    void rescan();

signals:
    void updated();

private:
    void armWatcher();
    void scanZone(const Zones::Zone &z, QHash<QString, LiveEntry> &fresh,
                  QHash<QString, QList<LiveEntry>> &freshAll);

    QHash<QString, LiveEntry> entries;
    QHash<QString, QList<LiveEntry>> allEntries;
    QFileSystemWatcher *watcher = 0;
    QTimer timer;
};
