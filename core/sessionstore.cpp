#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "sessionstore.h"

QString SessionStore::dataDir() {
    QString d = QDir::homePath() + "/.local/share/rezoom";
    QDir().mkpath(d);

    return d;
}

SessionStore::SessionStore(QObject *parent) : QObject(parent) {
    path = dataDir() + "/sessions.json";
    const bool existed = QFile::exists(path);
    load();

    if (!existed) {
        importLegacySnapshot();

        if (!chatList.isEmpty())
            save();
    }
}

void SessionStore::load() {
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object()["chats"].toArray();
    chatList.clear();

    for (const auto &v : arr)
        chatList.append(Chat::fromJson(v.toObject()));
}

void SessionStore::save() {
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
    chatList.append(c);
    save();
    emit changed();
}

void SessionStore::update(const Chat &c) {
    for (Chat &existing : chatList) {
        if (existing.id == c.id) {
            existing = c;
            save();
            emit changed();
            return;
        }
    }
}

void SessionStore::remove(const QString &id) {
    for (int i = 0; i < chatList.size(); ++i) {
        if (chatList[i].id == id) {
            chatList.removeAt(i);
            save();
            emit changed();
            return;
        }
    }
}

void SessionStore::touch(const QString &id) {
    for (Chat &c : chatList) {
        if (c.id == id) {
            c.lastActiveAt = QDateTime::currentMSecsSinceEpoch();
            save();
            emit changed();
            return;
        }
    }
}

// One-time seed from claude-freeze's snapshot:
// cwd \t sid \t exact|guessed \t "mm-dd HH:MM | preview"
void SessionStore::importLegacySnapshot() {
    QFile f(QDir::homePath() + "/.claude/session-snapshot.tsv");

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

        chatList.append(c);
    }
}
