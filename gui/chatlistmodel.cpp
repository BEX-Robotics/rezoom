#include <algorithm>

#include <QDateTime>

#include "core/liveregistry.h"
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
        return QStringLiteral("\xe2\x9c\xb3 working\xe2\x80\xa6"); // ✳ + … (U+2733, ellipsis)

    if (status == "idle")
        return QStringLiteral("waiting for you");

    if (status == "shell")
        return QStringLiteral("at shell");

    return fallback;
}

ChatListModel::ChatListModel(SessionStore *store, LiveRegistry *registry, QObject *parent)
    : QAbstractListModel(parent), store(store), registry(registry) {

    connect(store, &SessionStore::changed, this, &ChatListModel::rebuild);
    connect(registry, &LiveRegistry::updated, this, &ChatListModel::rebuild);
    rebuild();
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
        if (c.archived != showArchived)
            continue;

        if (!filter.isEmpty() && !c.title.contains(filter, Qt::CaseInsensitive)
            && !c.preview.contains(filter, Qt::CaseInsensitive)
            && !c.cwd.contains(filter, Qt::CaseInsensitive))
            continue;

        Sortable s = {};
        const auto live = registry->entryForSession(c.claudeSessionID);
        s.running = live.has_value();
        s.lastActive = s.running ? live->updatedAt : c.lastActiveAt;
        if (s.lastActive < c.lastActiveAt)
            s.lastActive = c.lastActiveAt;

        s.row.id = c.id;
        s.row.title = c.title.isEmpty() ? c.kind : c.title;
        s.row.status = s.running ? live->status : QStringLiteral("off");
        s.row.preview = statusPreview(s.row.status, c.preview);
        s.row.timeText = relativeTime(s.lastActive);
        s.row.tintHex = Chat::tintColorHex(c.tint);
        s.row.monogram = c.monogram();
        s.row.kind = c.kind;
        s.row.unread = unreadIDs.contains(c.id);
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
    case Qt::ToolTipRole: return r.title;
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
