#pragma once
#include <QAbstractListModel>
#include <QSet>

class SessionStore;
class LiveRegistry;
class NotificationWatcher;

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
    };

    ChatListModel(SessionStore *store, LiveRegistry *registry,
                  NotificationWatcher *notifications, QObject *parent = 0);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setFilter(const QString &text);
    void setShowArchived(bool on);
    void setUnread(const QSet<QString> &ids);
    QString idAt(const QModelIndex &index) const;
    QModelIndex indexOf(const QString &id) const;
    int archivedCount() const;

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
        bool unread = false;
    };

    SessionStore *store = 0;
    LiveRegistry *registry = 0;
    NotificationWatcher *notifications = 0;
    QList<Row> rows;
    QString filter;
    QSet<QString> unreadIDs;
    bool showArchived = false;
};
