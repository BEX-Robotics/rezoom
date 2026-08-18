#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// /proc helpers: what runs under our embedded shells, which ssh clients and
// tmux sessions exist. Read-only — never connects anywhere.
namespace ProcessScout {

struct ProcInfo {
    int pid = 0;
    QString comm;
    QStringList cmdline;
};

QList<int> children(int pid);
QList<int> descendants(int pid); // BFS, bounded
QString comm(int pid);
QStringList cmdline(int pid);

// All descendants of shellPID whose comm is in `names` (e.g. claude/ssh/tmux).
QList<ProcInfo> findDescendants(int shellPID, const QStringList &names);

struct TmuxSession {
    QString name;
    bool attached = false;
};

QList<TmuxSession> tmuxSessions();

// Running ssh client processes owned by this user.
QList<ProcInfo> runningSsh();

// Best-effort ssh destination from a client cmdline ("user@host" or "host").
QString sshDestination(const QStringList &cmdline);
}
