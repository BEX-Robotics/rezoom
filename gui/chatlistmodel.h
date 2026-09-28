#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QSet>

#include "core/sessionhealth.h"

struct Chat;
class SessionStore;
class LiveRegistry;
class NotificationWatcher;
class Templates;

// Left-panel list: chats joined with live presence, filtered and sorted
// WhatsApp-style (running first, then most recently active).
class ChatListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        TimeRole,
        StatusRole, // "busy" | "idle" | "shell" | "off"
        TintRole,   // "#rrggbb"
        MonogramRole,
        UnreadRole,
        KindRole,
        ZoneRole,  // account zone name, "" = default
    };

    ChatListModel(SessionStore *store, LiveRegistry *registry,
                  NotificationWatcher *notifications, Templates *templates,
                  QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setFilter(const QString &text);
    void setShowArchived(bool on);
    void setUnread(const QSet<QString> &ids);
    void setEmbedded(const QSet<QString> &ids);
    void setSshEnded(const QSet<QString> &ids);
    void setRemoteStates(const QHash<QString, QString> &states);
    void setLiveTitles(const QHash<QString, QString> &titles);
    void setLivePreviews(const QHash<QString, QString> &previews);
    QString idAt(const QModelIndex &index) const;
    QModelIndex indexOf(const QString &id) const;
    int archivedCount() const;
    bool anyWithStatus(const QString &status) const;

public slots:
    void rebuild();

private:
    struct Row {
        QString id;
        QString title;
        QString preview;
        QString timeText;
        QString status;
        QString tooltip;
        QString tintHex;
        QString monogram;
        QString kind;
        QString zone;
        bool unread = false;
    };

    Row makeRow(const Chat &c, const QString &liveStatus) const;
    QList<Row> buildRows() const;
    void applyHealthPreview(Row &row, const SessionHealth::Health &h) const;
    QString tooltipFor(const Chat &c, const Row &row) const;
    bool matchesFilter(const Chat &c) const;
    QString twinsWarning(const Chat &c) const;

    SessionStore *store = 0;
    LiveRegistry *registry = 0;
    NotificationWatcher *notifications = 0;
    Templates *templates = 0;
    QList<Row> rows;
    QString filter;
    QSet<QString> unreadIDs;
    QSet<QString> embeddedIDs;
    QSet<QString> sshEndedIDs; // panes whose ssh exited (until reconnect/dismiss)
    QHash<QString, QString> remoteStates; // external ssh chats: busy / idle / live
    QHash<QString, QString> liveTitles;   // display-only, e.g. konsole caption
    QHash<QString, QString> livePreviews; // display-only, busy-session tail
    bool showArchived = false;
};
