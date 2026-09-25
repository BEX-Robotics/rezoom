#include <algorithm>

#include <QDateTime>

#include "core/liveregistry.h"
#include "core/notifications.h"
#include "core/sessionstore.h"
#include "core/templates.h"

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

    if (status == "idle" && fallback.isEmpty())
        return QStringLiteral("waiting for you");

    if (status == "idle")
        return fallback; // the amber dot already says "waiting"

    if (status == "shell")
        return QStringLiteral("at shell");

    if (status == "live")
        return fallback.isEmpty() ? QStringLiteral("session open") : fallback;

    return fallback;
}

ChatListModel::ChatListModel(SessionStore *store, LiveRegistry *registry,
                             NotificationWatcher *notifications, Templates *templates,
                             QObject *parent)
    : QAbstractListModel(parent), store(store), registry(registry),
      notifications(notifications), templates(templates) {
    connect(store, &SessionStore::changed, this, &ChatListModel::rebuild);
    connect(registry, &LiveRegistry::updated, this, &ChatListModel::rebuild);
    connect(notifications, &NotificationWatcher::updated, this, &ChatListModel::rebuild);
    rebuild();
}

static QString agoText(qint64 ms) {
    const qint64 mins = (QDateTime::currentMSecsSinceEpoch() - ms) / 60000;

    if (mins < 1)
        return QObject::tr("just now");

    if (mins < 60)
        return QObject::tr("%n min ago", 0, mins);

    if (mins < 48 * 60)
        return QObject::tr("%n h ago", 0, mins / 60);

    return QDateTime::fromMSecsSinceEpoch(ms).toString("MMM d");
}

// "80b34689\xe2\x80\xa6bbd51" — recognizable, never overflows ("…" = ellipsis).
static QString shortSid(const QString &sid) {
    return sid.size() > 16 ? sid.left(8) + QString::fromUtf8("\xe2\x80\xa6") + sid.right(5) : sid;
}

static QString statusLine(const QString &status) {
    struct Entry {
        const char *key;
        const char *color;
        const char *text;
    };
    static const Entry entries[] = {
        {"busy", "#3fa34d", QT_TR_NOOP("working")},
        {"live", "#3fa34d", QT_TR_NOOP("session open")},
        {"idle", "#e6a817", QT_TR_NOOP("waiting for you")},
        {"shell", "#3a7bd5", QT_TR_NOOP("at shell")},
        {"frozen", "#d64545", QT_TR_NOOP("frozen")},
    };

    for (const Entry &e : entries)
        if (status == QLatin1String(e.key))
            // \xe2\x97\x8f = UTF-8 for "●" (filled circle)
            return QStringLiteral("<span style='color:%1'>\xe2\x97\x8f</span> %2")
                .arg(QLatin1String(e.color), QObject::tr(e.text));

    // \xe2\x97\x8b = UTF-8 for "○" (hollow circle)
    return QStringLiteral("\xe2\x97\x8b ") + QObject::tr("resumable");
}

static QString elide(const QString &s, int max) {
    // \xe2\x80\xa6 = UTF-8 for "…" (ellipsis)
    return s.size() > max ? s.left(max) + QString::fromUtf8("\xe2\x80\xa6") : s;
}

// Compact hover card: what it is, where it lives, how it comes back — and a
// hint to the copy menu, since tooltips can't be copied from.
QString ChatListModel::tooltipFor(const Chat &c, const Row &row) const {
    QStringList lines;
    lines << QStringLiteral("<b>%1</b>").arg(elide(row.title, 60).toHtmlEscaped());
    QString meta = c.kind + QString::fromUtf8("  \xc2\xb7  ") + statusLine(row.status); // "·"

    if (c.lastActiveAt)
        meta += QString::fromUtf8("  \xc2\xb7  ") + agoText(c.lastActiveAt);

    lines << meta;

    if (!c.cwd.isEmpty() || !c.host.isEmpty()) {
        // \xf0\x9f\x93\x81 = UTF-8 for the folder emoji
        QString where = QString::fromUtf8("\xf0\x9f\x93\x81 ")
            + Chat::tildify(c.cwd).toHtmlEscaped();

        if (!c.host.isEmpty())
            where += tr(" on %1").arg(c.host.toHtmlEscaped());

        lines << where;
    }

    if (!c.claudeSessionID.isEmpty())
        lines << QStringLiteral("<code>%1</code>").arg(shortSid(c.claudeSessionID));

    const QString resume = templates->resolveFor(c);

    if (!resume.isEmpty())
        // \xe2\x86\xa9 = UTF-8 for "↩" (return arrow)
        lines << QString::fromUtf8("\xe2\x86\xa9 <code>%1</code>")
                     .arg(elide(resume, 56).toHtmlEscaped());

    const QList<LiveEntry> twins = registry->entriesForSession(c.claudeSessionID);

    if (twins.size() > 1) {
        QStringList pids;

        for (const LiveEntry &e : twins)
            pids << QString::number(e.pid);

        // \xe2\x9a\xa0 = UTF-8 for the warning sign
        lines << QStringLiteral("<span style='color:#d64545'>\xe2\x9a\xa0 %1</span>")
                     .arg(tr("running in %1 terminals at once (pids %2) \xe2\x80\x94 close the extras")
                              // \xe2\x80\x94 = em dash
                              .arg(twins.size())
                              .arg(pids.join(", ")));
    }

    if (const auto fr = notifications->freezeFor(c.claudeSessionID))
        lines << QStringLiteral("<span style='color:#d64545'>%1</span>")
                     .arg(elide(fr->message, 70).toHtmlEscaped());

    lines << QStringLiteral("<span style='color:gray'>%1</span>")
                 .arg(tr("right-click to copy id / command"));
    return lines.join(QStringLiteral("<br>"));
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

    // Live display overrides: konsole caption as title, busy-tail as preview.
    const QString lt = liveTitles.value(c.id);

    if (!lt.isEmpty()) {
        row.title = lt;
        row.tooltip = lt;
    }

    const QString lp = livePreviews.value(c.id);

    if (!lp.isEmpty())
        row.preview = lp;

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

    row.tooltip = tooltipFor(c, row);
    return row;
}

bool ChatListModel::matchesFilter(const Chat &c) const {
    if (filter.isEmpty())
        return true;

    return c.title.contains(filter, Qt::CaseInsensitive)
        || liveTitles.value(c.id).contains(filter, Qt::CaseInsensitive)
        || c.preview.contains(filter, Qt::CaseInsensitive)
        || c.cwd.contains(filter, Qt::CaseInsensitive);
}

// Order = persisted recency alone, WhatsApp-style: stable across restarts,
// a chat moves only when something actually happened in it (launch, turn
// finished) — never because presence flickered.
QList<ChatListModel::Row> ChatListModel::buildRows() const {
    struct Sortable {
        Row row;
        qint64 lastActive = 0;
    };
    QList<Sortable> tmp;

    for (const Chat &c : store->chats()) {
        if (c.archived != showArchived || !matchesFilter(c))
            continue;

        Sortable s = {};
        const auto live = registry->entryForSession(c.claudeSessionID);
        QString status = live ? live->status : QStringLiteral("off");

        // Codex & friends have no live registry — an open pane means "live".
        if (!live && embeddedIDs.contains(c.id))
            status = QStringLiteral("live");

        s.lastActive = c.lastActiveAt;
        s.row = makeRow(c, status);
        s.row.timeText = relativeTime(s.lastActive);
        tmp.append(s);
    }

    std::stable_sort(tmp.begin(), tmp.end(), [](const Sortable &a, const Sortable &b) {
        return a.lastActive > b.lastActive;
    });

    QList<Row> out;

    for (const Sortable &s : tmp)
        out.append(s.row);

    return out;
}

// Same row set in the same order → in-place dataChanged (no reset, no
// flicker, selection survives). Structure changed → full reset.
void ChatListModel::rebuild() {
    QList<Row> fresh = buildRows();
    bool sameShape = fresh.size() == rows.size();

    for (int i = 0; sameShape && i < fresh.size(); ++i)
        if (fresh[i].id != rows[i].id)
            sameShape = false;

    if (sameShape) {
        rows = fresh;

        if (!rows.isEmpty())
            emit dataChanged(index(0, 0), index(rows.size() - 1, 0));

        return;
    }

    beginResetModel();
    rows = fresh;
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

void ChatListModel::setLiveTitles(const QHash<QString, QString> &titles) {
    liveTitles = titles;
    rebuild();
}

void ChatListModel::setLivePreviews(const QHash<QString, QString> &previews) {
    livePreviews = previews;
    rebuild();
}

void ChatListModel::setEmbedded(const QSet<QString> &ids) {
    embeddedIDs = ids;
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
