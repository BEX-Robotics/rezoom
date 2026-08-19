#pragma once
#include <functional>

#include <QList>
#include <QObject>
#include <QTimer>

#include "chat.h"

class QFileSystemWatcher;

// Persistent chat list: sessions.json under dataDir() (~/.local/share/rezoom,
// overridable via $REZOOM_DATA_DIR for tests).
//
// Multi-process safe: every mutation is a read-modify-write of the on-disk
// state under a lock file, so concurrent GUI instances and rezoom-cli never
// clobber each other's writes; external writes are picked up live through a
// directory watcher and surface as the same changed() signal.
class SessionStore : public QObject {
    Q_OBJECT
public:
    explicit SessionStore(QObject *parent = 0);

    const QList<Chat> &chats() const { return chatList; }

    const Chat *find(const QString &id) const;
    const Chat *findByClaudeSession(const QString &sessionID) const;

    void add(const Chat &c);
    void addBatch(const QList<Chat> &cs); // one lock/write for the lot
    void update(const Chat &c);           // by id; a concurrent delete wins
    void remove(const QString &id);
    void touch(const QString &id);        // bump lastActiveAt to now

    // Atomic compound operation: op sees the freshest on-disk state under the
    // lock — read-and-decide inside op when the decision must not race (e.g.
    // dedup-then-add across processes).
    void mutate(const std::function<void(QList<Chat> &)> &op);

    static QString dataDir();

signals:
    void changed();

private slots:
    void reloadIfChanged();

private:
    void loadFromDisk();
    void writeToDisk();
    void rememberStamp();
    static void importLegacySnapshot(QList<Chat> &list);

    QList<Chat> chatList;
    QString path;
    QFileSystemWatcher *watcher = 0;
    QTimer reloadTimer;
    qint64 knownMtimeMs = 0;
    qint64 knownSize = -1;
};
