#include <algorithm>

#include <QDateTime>

#include "core/liveregistry.h"
#include "core/notifications.h"
#include "core/sessionstore.h"

#include "chatlistmodel.h"

static QString relativeTime(qint64 ms) {
    if (!ms)
        return {};

    const QDateTime t = QDateTime::fromMSecsSinceEpoch(ms);

    if (t.date() == QDate::currentDate())
        return t.toString("HH:mm");

    if (t.date().year() == QDate::currentDate().year())
        return t.toString("MMM d");

    return t.toString("yyyy-MM-dd");
}

static QString statusPreview(const QString &status, const QString &fallback) {
    if (status == "busy")
        // \xe2\x9c\xb3 = UTF-8 for "✳", \xe2\x80\xa6 = "…" (ellipsis)
        return QString::fromUtf8("\xe2\x9c\xb3 working\xe2\x80\xa6");

    if (status == "idle")
        return QStringLiteral("waiting for you");

    if (status == "shell")
        return QStringLiteral("at shell");

    return fallback;
}

ChatListModel::ChatListModel(SessionStore *store, LiveRegistry *registry,
                             NotificationWatcher *notifications, QObject *parent)
    : QAbstractListModel(parent), store(store), registry(registry),
      notifications(notifications) {

    connect(store, &SessionStore::changed, this, &ChatListModel::rebuild);
    connect(registry, &LiveRegistry::updated, this, &ChatListModel::rebuild);
    connect(notifications, &NotificationWatcher::updated, this, &ChatListModel::rebuild);
    rebuild();
}

// Everything about one list row except sort keys and time text.
ChatListModel::Row ChatListModel::makeRow(const Chat &c, const QString &status) const {
    Row row = {};
    row.id = c.id;
    row.title = c.title.isEmpty() ? c.kind : c.title;
    row.status = status;
    row.preview = statusPreview(status, c.preview);
    row.tooltip = row.title;
    row.tintHex = Chat::tintColorHex(c.tint);
    row.monogram = c.monogram();
    row.kind = c.kind;
    row.unread = unreadIDs.contains(c.id);

    // A limit-freeze reported by the Notification hook overrides presence.
    if (const auto fr = notifications->freezeFor(c.claudeSessionID)) {
        row.status = QStringLiteral("frozen");
        row.tooltip = fr->message;

        // \xe2\x9b\x94 = UTF-8 for the no-entry sign, \xe2\x80\x94 = em dash
        row.preview = fr->resetAtMs
            ? QString::fromUtf8("\xe2\x9b\x94 frozen \xe2\x80\x94 resets at %1")
                  .arg(QDateTime::fromMSecsSinceEpoch(fr->resetAtMs).toString("HH:mm"))
            : QString::fromUtf8("\xe2\x9b\x94 frozen \xe2\x80\x94 %1").arg(fr->message.left(60));
    }

    return row;
}

bool ChatListModel::matchesFilter(const Chat &c) const {
    if (filter.isEmpty())
        return true;

    return c.title.contains(filter, Qt::CaseInsensitive)
        || c.preview.contains(filter, Qt::CaseInsensitive)
        || c.cwd.contains(filter, Qt::CaseInsensitive);
}

void ChatListModel::rebuild() {
    beginResetModel();
    rows.clear();

    struct Sortable {
        Row row;
        bool running = false;
        qint64 lastActive = 0;
    };
    QList<Sortable> tmp;

    for (const Chat &c : store->chats()) {
        if (c.archived != showArchived || !matchesFilter(c))
            continue;

        Sortable s = {};
        const auto live = registry->entryForSession(c.claudeSessionID);
        s.running = live.has_value();
        s.lastActive = qMax(s.running ? live->updatedAt : c.lastActiveAt, c.lastActiveAt);
        s.row = makeRow(c, s.running ? live->status : QStringLiteral("off"));
        s.row.timeText = relativeTime(s.lastActive);
        tmp.append(s);
    }

    std::sort(tmp.begin(), tmp.end(), [](const Sortable &a, const Sortable &b) {
        if (a.running != b.running)
            return a.running;

        return a.lastActive > b.lastActive;
    });

    for (const Sortable &s : tmp)
        rows.append(s.row);

    endResetModel();
}

int ChatListModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : rows.size();
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= rows.size())
        return {};

    const Row &r = rows[index.row()];

    switch (role) {
    case IdRole:       return r.id;
    case TitleRole:    return r.title;
    case PreviewRole:  return r.preview;
    case TimeRole:     return r.timeText;
    case StatusRole:   return r.status;
    case TintRole:     return r.tintHex;
    case MonogramRole: return r.monogram;
    case UnreadRole:   return r.unread;
    case KindRole:     return r.kind;
    case Qt::ToolTipRole: return r.tooltip;
    }

    return {};
}

void ChatListModel::setFilter(const QString &text) {
    filter = text.trimmed();
    rebuild();
}

void ChatListModel::setShowArchived(bool on) {
    showArchived = on;
    rebuild();
}

void ChatListModel::setUnread(const QSet<QString> &ids) {
    unreadIDs = ids;
    rebuild();
}

QString ChatListModel::idAt(const QModelIndex &index) const {
    return data(index, IdRole).toString();
}

QModelIndex ChatListModel::indexOf(const QString &id) const {
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].id == id)
            return index(i, 0);

    return {};
}

int ChatListModel::archivedCount() const {
    int n = 0;

    for (const Chat &c : store->chats())
        if (c.archived)
            ++n;

    return n;
}
