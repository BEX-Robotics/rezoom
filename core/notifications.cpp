#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "notifications.h"
#include "sessionstore.h"

NotificationWatcher::NotificationWatcher(QObject *parent) : QObject(parent) {
    path = SessionStore::dataDir() + "/notifications.jsonl";
    watcher = new QFileSystemWatcher(this);
    watcher->addPath(SessionStore::dataDir()); // file may not exist yet
    connect(watcher, &QFileSystemWatcher::directoryChanged, this,
            &NotificationWatcher::readNew);
    connect(&timer, &QTimer::timeout, this, &NotificationWatcher::readNew);
    timer.start(3000);
    readNew();
}

bool NotificationWatcher::looksFrozen(const QString &message) {
    static const QRegularExpression re(
        "limit (reached|hit)|(usage|rate|weekly|session|5-hour) limit|resets at"
        "|out of (credits?|usage)|credit balance|insufficient credit",
        QRegularExpression::CaseInsensitiveOption);

    return re.match(message).hasMatch();
}

// Best-effort "resets at 3pm" / "resets at 14:30" → ms since epoch (today,
// or tomorrow if already past). 0 when unparseable — the raw text is still
// shown, we just can't count down.
qint64 NotificationWatcher::parseResetTime(const QString &message, qint64 nowMs) {
    static const QRegularExpression re(
        "resets? at ([0-9]{1,2})(?::([0-9]{2}))?\\s*(am|pm)?",
        QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(message);

    if (!m.hasMatch())
        return 0;

    int hour = m.captured(1).toInt();
    const int minute = m.captured(2).toInt();
    const QString half = m.captured(3).toLower();

    if (half == "pm" && hour < 12)
        hour += 12;

    if (half == "am" && hour == 12)
        hour = 0;

    if (hour > 23 || minute > 59)
        return 0;

    const QDateTime now = QDateTime::fromMSecsSinceEpoch(nowMs);
    QDateTime reset(now.date(), QTime(hour, minute));

    if (reset.toMSecsSinceEpoch() <= nowMs)
        reset = reset.addDays(1);

    return reset.toMSecsSinceEpoch();
}

void NotificationWatcher::readNew() {
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return;

    if (f.size() < offset)
        offset = 0; // truncated/rotated — start over

    if (f.size() == offset)
        return;

    f.seek(offset);
    bool changed = false;

    while (!f.atEnd()) {
        const QByteArray line = f.readLine();
        const QJsonObject o = QJsonDocument::fromJson(line).object();
        const QString sid = o["session_id"].toString();

        if (sid.isEmpty())
            continue;

        const QString msg = o["message"].toString();

        if (looksFrozen(msg)) {
            FreezeInfo info = {};
            info.message = msg;
            info.ts = static_cast<qint64>(o["ts"].toDouble());

            if (!info.ts)
                info.ts = QDateTime::currentMSecsSinceEpoch();

            info.resetAtMs = parseResetTime(msg, info.ts);
            freezes.insert(sid, info);
            changed = true;
        } else if (freezes.remove(sid))
            changed = true; // spoke again about something else — not frozen
    }

    offset = f.pos();

    if (changed)
        emit updated();
}

std::optional<FreezeInfo> NotificationWatcher::freezeFor(const QString &sessionID) const {
    const auto it = freezes.constFind(sessionID);

    if (it == freezes.constEnd())
        return std::nullopt;

    return *it;
}

QStringList NotificationWatcher::frozenSessions() const {
    return freezes.keys();
}

void NotificationWatcher::clear(const QString &sessionID) {
    if (freezes.remove(sessionID))
        emit updated();
}
