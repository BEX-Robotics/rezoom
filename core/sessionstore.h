#pragma once
#include <QList>
#include <QObject>

#include "chat.h"

// Persistent chat list: ~/.local/share/rezoom/sessions.json (atomic writes).
class SessionStore : public QObject {
    Q_OBJECT
public:
    explicit SessionStore(QObject *parent = 0);

    const QList<Chat> &chats() const { return chatList; }

    const Chat *find(const QString &id) const;
    const Chat *findByClaudeSession(const QString &sessionID) const;

    void add(const Chat &c);
    void update(const Chat &c); // matched by id
    void remove(const QString &id);
    void touch(const QString &id); // bump lastActiveAt to now

    static QString dataDir();

signals:
    void changed();

private:
    void load();
    void save();
    void importLegacySnapshot(); // one-time seed from ~/.claude/session-snapshot.tsv

    QList<Chat> chatList;
    QString path;
};
