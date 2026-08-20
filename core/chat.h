#pragma once
#include <QJsonObject>
#include <QString>

// One "chat" in the left panel: a claude session, a plain shell, an ssh
// connection or a tmux session — anything with a way to bring it back.
struct Chat {
    QString id;               // rezoom-internal uuid
    QString title;            // user-visible name (renamable)
    QString kind;             // "claude" | "shell" | "ssh" | "tmux"
    QString claudeSessionID;  // claude session uuid (may be empty)
    QString cwd;              // working directory (local or remote)
    QString host;             // ssh destination, empty for local
    QString entryCommand;     // recorded "how we entered" (e.g. "ssh dev.example.com")
    QString templateName;     // resumable-command template; empty = auto by kind
    QString commandOverride;  // raw command override; wins over template
    QString tmuxSession;      // tmux session name
    QString preview;          // first-user-message snippet (WhatsApp-style)
    QString tint;             // konsole profile name (tint-red, ...) → avatar color
    bool titleLocked = false; // user renamed it — auto-titling keeps off
    bool archived = false;
    qint64 createdAt = 0;     // ms since epoch
    qint64 lastActiveAt = 0;

    static Chat create(const QString &kind);
    static QString randomTint(const QString &seed);
    static QString tintColorHex(const QString &tint); // "#rrggbb" for the avatar

    QJsonObject toJson() const;
    static Chat fromJson(const QJsonObject &o);
    QString monogram() const; // 1-2 chars for the avatar circle
};
