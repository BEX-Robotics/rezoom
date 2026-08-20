#pragma once
#include <optional>

#include <QHash>
#include <QObject>
#include <QTimer>

class QFileSystemWatcher;

// A per-session freeze event (usage/credit limit banner), captured by the
// Notification hook (rezoom-notify-hook) appending to
// <dataDir>/notifications.jsonl.
struct FreezeInfo {
    QString message;
    qint64 ts = 0;        // when the notification arrived (ms since epoch)
    qint64 resetAtMs = 0; // parsed "resets at ..." when recognizable, else 0
};

// Tails notifications.jsonl and keeps the current freeze state per claude
// session id. A later non-freeze notification (e.g. a permission prompt) or
// the session going busy again clears the freeze.
class NotificationWatcher : public QObject {
    Q_OBJECT
public:
    explicit NotificationWatcher(QObject *parent = 0);

    std::optional<FreezeInfo> freezeFor(const QString &sessionID) const;
    QStringList frozenSessions() const;
    void clear(const QString &sessionID);

    static bool looksFrozen(const QString &message);
    static qint64 parseResetTime(const QString &message, qint64 nowMs);

signals:
    void updated();

private slots:
    void readNew();

private:
    QString path;
    qint64 offset = 0;
    QHash<QString, FreezeInfo> freezes;
    QFileSystemWatcher *watcher = 0;
    QTimer timer;
};
