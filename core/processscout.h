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

// This user's processes with the given comm.
QList<ProcInfo> byComm(const QString &name);

// Running ssh client processes owned by this user.
QList<ProcInfo> runningSsh();

// Best-effort ssh destination from a client cmdline ("user@host" or "host").
QString sshDestination(const QStringList &cmdline);

// Parent pid, 0 when unknown.
int parentPid(int pid);

// Stopped by a signal — e.g. Ctrl+Z in its terminal. The process is alive
// but frozen, and can't update its own status files.
bool isStopped(int pid);

// Process group (the shell job it belongs to); 0 when unknown.
int processGroup(int pid);

// Nearest ancestor whose comm matches (0 = none) — e.g. the konsole window
// hosting a claude, or detecting tool-spawned claude-under-claude.
int ancestorPidOfComm(int pid, const QString &comm);
bool hasAncestorComm(int pid, const QString &comm);

// The pty a process reads from (/proc/<pid>/fd/0 target) — empty on macOS.
// Comparing before/after tells whether a reptyr move actually happened.
QString tty(int pid);
}
