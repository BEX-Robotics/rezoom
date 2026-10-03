#include <cerrno>
#include <csignal>

#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>

#include "liveregistry.h"
#include "processscout.h"
#include "zones.h"

static QString sessionsDir(const Zones::Zone &z) {
    return z.dir + "/sessions";
}

// Watch every zone's sessions dir; dirs can appear later (first claude run
// in a new zone), so this re-arms on each rescan.
void LiveRegistry::armWatcher() {
    const QStringList watched = watcher->directories();

    for (const Zones::Zone &z : Zones::all())
        if (!watched.contains(sessionsDir(z)) && QDir(sessionsDir(z)).exists())
            watcher->addPath(sessionsDir(z));
}

LiveRegistry::LiveRegistry(QObject *parent) : QObject(parent) {
    watcher = new QFileSystemWatcher(this);
    armWatcher();

    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &LiveRegistry::rescan);

    // Status flips (idle/busy) rewrite file contents the dir-watcher can
    // miss, so poll too; 25 tiny files every 2 s is nothing.
    connect(&timer, &QTimer::timeout, this, &LiveRegistry::rescan);
    timer.start(2000);
    rescan();
}

bool autoAdoptable(const LiveEntry &e) {
    if (e.kind != QLatin1String("interactive"))
        return false;

    for (const char *scratch : {"/tmp/", "/var/tmp/", "/run/", "/dev/"})
        if (e.cwd.startsWith(QLatin1String(scratch)))
            return false;

    return !ProcessScout::hasAncestorComm(e.pid, QStringLiteral("claude"));
}

bool LiveRegistry::pidAlive(int pid) {
    // kill(pid, 0) instead of /proc so this works on macOS too.
    return pid > 0 && (::kill(pid, 0) == 0 || errno == EPERM);
}

static std::optional<LiveEntry> readPidFileIn(const Zones::Zone &z, int pid) {
    QFile f(sessionsDir(z) + QStringLiteral("/%1.json").arg(pid));

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

    // A file caught mid-write can lack updatedAt; startedAt is written first.
    e.updatedAt = static_cast<qint64>(o["updatedAt"].toDouble(o["startedAt"].toDouble()));
    e.zone = z.name;

    if (e.sessionID.isEmpty() || !LiveRegistry::pidAlive(e.pid))
        return std::nullopt;

    return e;
}

std::optional<LiveEntry> LiveRegistry::readPidFile(int pid) {
    for (const Zones::Zone &z : Zones::all())
        if (const auto e = readPidFileIn(z, pid))
            return e;

    return std::nullopt;
}

void LiveRegistry::rescan() {
    QHash<QString, LiveEntry> fresh;
    QHash<QString, QList<LiveEntry>> freshAll;

    for (const Zones::Zone &z : Zones::all())
        scanZone(z, fresh, freshAll);

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

    allEntries = freshAll;

    if (!same) {
        entries = fresh;
        emit updated();
    }

    armWatcher();
}

void LiveRegistry::scanZone(const Zones::Zone &z, QHash<QString, LiveEntry> &fresh,
                            QHash<QString, QList<LiveEntry>> &freshAll) {
    const QStringList files = QDir(sessionsDir(z)).entryList({"*.json"}, QDir::Files);

    for (const QString &fn : files) {
        bool ok = false;
        const int pid = fn.chopped(5).toInt(&ok); // strip ".json"

        if (!ok)
            continue;

        const auto e = readPidFileIn(z, pid);

        if (!e)
            continue;

        freshAll[e->sessionID].append(*e);

        // Deduped view keeps the freshest writer.
        const auto it = fresh.constFind(e->sessionID);

        if (it == fresh.constEnd() || it->updatedAt < e->updatedAt)
            fresh.insert(e->sessionID, *e);
    }
}

std::optional<LiveEntry> LiveRegistry::entryForSession(const QString &sessionID) const {
    const auto it = entries.constFind(sessionID);

    if (it == entries.constEnd())
        return std::nullopt;

    return *it;
}

QList<LiveEntry> LiveRegistry::entriesForSession(const QString &sessionID) const {
    return allEntries.value(sessionID);
}

std::optional<LiveEntry> LiveRegistry::entryForPID(int pid) const {
    for (const LiveEntry &e : entries)
        if (e.pid == pid)
            return e;

    return readPidFile(pid); // may be newer than the last rescan
}
