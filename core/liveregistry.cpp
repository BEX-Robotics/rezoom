#include <cerrno>
#include <csignal>

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>

#include "liveregistry.h"

// $REZOOM_CLAUDE_DIR overrides ~/.claude for tests and staged demos.
static QString claudeDir() {
    const QString override = qEnvironmentVariable("REZOOM_CLAUDE_DIR");

    return override.isEmpty() ? QDir::homePath() + "/.claude" : override;
}

static QString sessionsDir() {
    return claudeDir() + "/sessions";
}

LiveRegistry::LiveRegistry(QObject *parent) : QObject(parent) {
    watcher = new QFileSystemWatcher(this);

    if (QDir(sessionsDir()).exists())
        watcher->addPath(sessionsDir());

    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &LiveRegistry::rescan);

    // Status flips (idle/busy) rewrite file contents the dir-watcher can
    // miss, so poll too; 25 tiny files every 2 s is nothing.
    connect(&timer, &QTimer::timeout, this, &LiveRegistry::rescan);
    timer.start(2000);
    rescan();
}

bool LiveRegistry::pidAlive(int pid) {
    // kill(pid, 0) instead of /proc so this works on macOS too.
    return pid > 0 && (::kill(pid, 0) == 0 || errno == EPERM);
}

std::optional<LiveEntry> LiveRegistry::readPidFile(int pid) {
    QFile f(sessionsDir() + QStringLiteral("/%1.json").arg(pid));

    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;

    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    LiveEntry e = {};
    e.pid = o["pid"].toInt();
    e.sessionID = o["sessionId"].toString();
    e.status = o["status"].toString();
    e.kind = o["kind"].toString();
    e.name = o["name"].toString();
    e.nameSource = o["nameSource"].toString();
    e.cwd = o["cwd"].toString();
    e.updatedAt = static_cast<qint64>(o["updatedAt"].toDouble());

    if (e.sessionID.isEmpty() || !pidAlive(e.pid))
        return std::nullopt;

    return e;
}

void LiveRegistry::rescan() {
    QHash<QString, LiveEntry> fresh;
    const QDir dir(sessionsDir());
    const QStringList files = dir.entryList({"*.json"}, QDir::Files);

    for (const QString &fn : files) {
        bool ok = false;
        const int pid = fn.chopped(5).toInt(&ok); // strip ".json"

        if (!ok)
            continue;

        const auto e = readPidFile(pid);

        if (!e)
            continue;

        // Same session id under two pids (stale file): keep the freshest.
        const auto it = fresh.constFind(e->sessionID);

        if (it == fresh.constEnd() || it->updatedAt < e->updatedAt)
            fresh.insert(e->sessionID, *e);
    }

    bool same = fresh.size() == entries.size();

    if (same) {
        for (auto it = fresh.constBegin(); it != fresh.constEnd(); ++it) {
            const auto old = entries.constFind(it.key());

            if (old == entries.constEnd() || old->pid != it->pid || old->status != it->status) {
                same = false;
                break;
            }
        }
    }

    if (!same) {
        entries = fresh;
        emit updated();
    }

    // The dir can appear after startup (first claude ever) — re-arm the watcher.
    if (watcher->directories().isEmpty() && QDir(sessionsDir()).exists())
        watcher->addPath(sessionsDir());
}

std::optional<LiveEntry> LiveRegistry::entryForSession(const QString &sessionID) const {
    const auto it = entries.constFind(sessionID);

    if (it == entries.constEnd())
        return std::nullopt;

    return *it;
}

std::optional<LiveEntry> LiveRegistry::entryForPID(int pid) const {
    for (const LiveEntry &e : entries)
        if (e.pid == pid)
            return e;

    return readPidFile(pid); // may be newer than the last rescan
}
