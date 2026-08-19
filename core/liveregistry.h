#pragma once
#include <optional>

#include <QHash>
#include <QObject>
#include <QTimer>

class QFileSystemWatcher;

// One running Claude Code session, as reported by claude itself in
// ~/.claude/sessions/<pid>.json.
struct LiveEntry {
    int pid = 0;
    QString sessionID;
    QString status; // "idle" | "busy" | "shell"
    QString kind;   // "interactive" for real CLI sessions
    QString name;
    QString cwd;
    qint64 updatedAt = 0;
};

// Watches ~/.claude/sessions/ — the exact live-session registry claude
// maintains (pid, sessionId, cwd, live status). Entries with dead pids
// are ignored and pruned on the fly.
class LiveRegistry : public QObject {
    Q_OBJECT
public:
    explicit LiveRegistry(QObject *parent = 0);

    const QHash<QString, LiveEntry> &bySessionID() const { return entries; }

    std::optional<LiveEntry> entryForSession(const QString &sessionID) const;
    std::optional<LiveEntry> entryForPID(int pid) const; // falls back to reading the file

    static std::optional<LiveEntry> readPidFile(int pid);
    static bool pidAlive(int pid);

public slots:
    void rescan();

signals:
    void updated();

private:
    QHash<QString, LiveEntry> entries;
    QFileSystemWatcher *watcher = 0;
    QTimer timer;
};
