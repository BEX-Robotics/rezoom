#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include "sessionstore.h"

QString SessionStore::dataDir() {
    QString d = qEnvironmentVariable("REZOOM_DATA_DIR");

    if (d.isEmpty())
        d = QDir::homePath() + "/.local/share/rezoom";

    QDir().mkpath(d);

    return d;
}

SessionStore::SessionStore(QObject *parent) : QObject(parent) {
    path = dataDir() + "/sessions.json";
    loadFromDisk();

    // First run: seed from claude-freeze's snapshot. The existence check is
    // repeated inside the lock — N processes racing on a fresh store must
    // yield exactly one import.
    if (!QFile::exists(path))
        mutate([p = path](QList<Chat> &list) {
            if (QFile::exists(p))
                return; // another process seeded the store first

            importLegacySnapshot(list);
        });

    watcher = new QFileSystemWatcher(this);
    watcher->addPath(dataDir());
    reloadTimer.setSingleShot(true);
    reloadTimer.setInterval(200);
    connect(watcher, &QFileSystemWatcher::directoryChanged, this,
            [this] { reloadTimer.start(); });
    connect(&reloadTimer, &QTimer::timeout, this, &SessionStore::reloadIfChanged);
}

// Every mutation re-reads the current on-disk state under the lock and
// re-applies just its own operation to it, so a write never publishes a
// stale copy of someone else's changes.
void SessionStore::mutate(const std::function<void(QList<Chat> &)> &op) {
    QLockFile lock(path + ".lock");

    if (!lock.tryLock(5000))
        qWarning("rezoom: sessions.json lock timed out after 5s — writing anyway");

    loadFromDisk();
    op(chatList);
    writeToDisk();
    emit changed();
}

void SessionStore::reloadIfChanged() {
    const QFileInfo fi(path);

    if (fi.lastModified().toMSecsSinceEpoch() == knownMtimeMs && fi.size() == knownSize)
        return;

    loadFromDisk();
    emit changed();
}

void SessionStore::rememberStamp() {
    const QFileInfo fi(path);
    knownMtimeMs = fi.lastModified().toMSecsSinceEpoch();
    knownSize = fi.size();
}

void SessionStore::loadFromDisk() {
    chatList.clear();
    rememberStamp();
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object()["chats"].toArray();

    for (const auto &v : arr)
        chatList.append(Chat::fromJson(v.toObject()));
}

void SessionStore::writeToDisk() {
    QJsonArray arr;

    for (const Chat &c : chatList)
        arr.append(c.toJson());

    QJsonObject root;
    root["chats"] = arr;
    QSaveFile f(path);

    if (!f.open(QIODevice::WriteOnly))
        return;

    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    f.commit();
    rememberStamp();
}

const Chat *SessionStore::find(const QString &id) const {
    for (const Chat &c : chatList)
        if (c.id == id)
            return &c;

    return 0;
}

const Chat *SessionStore::findByClaudeSession(const QString &sessionID) const {
    if (sessionID.isEmpty())
        return 0;

    for (const Chat &c : chatList)
        if (c.claudeSessionID == sessionID)
            return &c;

    return 0;
}

void SessionStore::add(const Chat &c) {
    mutate([&c](QList<Chat> &list) { list.append(c); });
}

void SessionStore::addBatch(const QList<Chat> &cs) {
    mutate([&cs](QList<Chat> &list) { list.append(cs); });
}

void SessionStore::update(const Chat &c) {
    mutate([&c](QList<Chat> &list) {
        for (Chat &existing : list) {
            if (existing.id == c.id) {
                existing = c;
                return;
            }
        }

        // id gone: deleted by another process meanwhile — deletion wins
    });
}

void SessionStore::remove(const QString &id) {
    mutate([&id](QList<Chat> &list) {
        list.removeIf([&id](const Chat &c) { return c.id == id; });
    });
}

void SessionStore::touch(const QString &id) {
    mutate([&id](QList<Chat> &list) {
        for (Chat &c : list)
            if (c.id == id)
                c.lastActiveAt = QDateTime::currentMSecsSinceEpoch();
    });
}

// One-time seed from claude-freeze's snapshot:
// cwd \t sid \t exact|guessed \t "mm-dd HH:MM | preview"
void SessionStore::importLegacySnapshot(QList<Chat> &list) {
    const QString claude = qEnvironmentVariable("REZOOM_CLAUDE_DIR").isEmpty()
        ? QDir::homePath() + "/.claude"
        : qEnvironmentVariable("REZOOM_CLAUDE_DIR");
    QFile f(claude + "/session-snapshot.tsv");

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        const QStringList parts = line.split('\t');

        if (parts.size() < 2)
            continue;

        Chat c = Chat::create("claude");
        c.cwd = parts[0];
        c.claudeSessionID = parts[1];

        if (parts.size() >= 4) {
            QString p = parts[3];
            const int bar = p.indexOf(" | ");

            if (bar >= 0)
                p = p.mid(bar + 3);

            c.preview = p;
            c.title = p.left(40).trimmed();
        }

        if (c.title.isEmpty())
            c.title = QDir(c.cwd).dirName();

        list.append(c);
    }
}
