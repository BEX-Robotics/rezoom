#include <QAbstractItemView>
#include <QHelpEvent>
#include <QPainter>
#include <QToolTip>

#include "chatlistmodel.h"

#include "chatdelegate.h"

static QColor statusColor(const QString &status) {
    if (status == "frozen")
        return QColor("#d64545"); // red: limit-frozen, waiting for reset

    if (status == "busy" || status == "live")
        return QColor("#3fa34d"); // green: working / session open in a pane

    if (status == "idle")
        return QColor("#e6a817"); // amber: waiting for the user

    if (status == "shell")
        return QColor("#3a7bd5"); // blue: sitting at a shell

    return {}; // off — drawn as a hollow ring
}

QSize ChatDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const {
    return QSize(240, 58);
}

static void paintBackground(QPainter *p, const QStyleOptionViewItem &opt, const QRect &r) {
    if (opt.state & QStyle::State_Selected) {
        QColor sel = opt.palette.highlight().color();
        sel.setAlpha(110);
        p->setPen(Qt::NoPen);
        p->setBrush(sel);
        p->drawRoundedRect(r, 8, 8);

        // Accent bar — the selection must be findable at a glance.
        p->setBrush(opt.palette.highlight().color());
        p->drawRoundedRect(QRect(r.left(), r.top() + 4, 4, r.height() - 8), 2, 2);
    } else if (opt.state & QStyle::State_MouseOver) {
        QColor hov = opt.palette.text().color();
        hov.setAlpha(14);
        p->setPen(Qt::NoPen);
        p->setBrush(hov);
        p->drawRoundedRect(r, 8, 8);
    }
}

static QRect zonePillRect(const QStyleOptionViewItem &opt, const QRect &line, int timeW,
                          const QString &zone);

// Every hot spot of a row, computed once so paint() and the per-part
// tooltips can never disagree about where things are.
struct RowGeometry {
    QRect row;
    QRect avatar;
    QRect dot;
    QRect line1;
    QRect line2;
    QRect unread;
    QRect pill;
};

static RowGeometry rowGeometry(const QStyleOptionViewItem &opt, const QModelIndex &index) {
    RowGeometry g = {};
    g.row = opt.rect.adjusted(6, 3, -6, -3);
    const int d = 40;
    g.avatar = QRect(g.row.left() + 6, g.row.center().y() - d / 2, d, d);
    g.dot = QRect(g.avatar.right() - 11, g.avatar.bottom() - 11, 12, 12);
    const int textLeft = g.avatar.right() + 10;
    g.line1 = QRect(textLeft, g.row.top() + 6, g.row.right() - textLeft - 4, g.row.height() / 2 - 6);
    g.line2 = QRect(textLeft, g.row.center().y(), g.row.right() - textLeft - 4, g.row.height() / 2 - 4);
    g.unread = QRect(g.row.right() - 14, g.line2.center().y() - 4, 9, 9);
    const int timeW = opt.fontMetrics.horizontalAdvance(index.data(ChatListModel::TimeRole).toString()) + 6;
    g.pill = zonePillRect(opt, g.line1, timeW, index.data(ChatListModel::ZoneRole).toString());

    return g;
}

static void paintAvatar(QPainter *p, const QStyleOptionViewItem &opt, const RowGeometry &g,
                        const QModelIndex &index) {

    const QRect &avatar = g.avatar;
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(index.data(ChatListModel::TintRole).toString()));
    p->drawEllipse(avatar);

    p->setPen(Qt::white);
    QFont mono = opt.font;
    mono.setBold(true);
    mono.setPointSizeF(opt.font.pointSizeF() * 1.1);
    p->setFont(mono);
    p->drawText(avatar, Qt::AlignCenter, index.data(ChatListModel::MonogramRole).toString());

    // Presence dot on the avatar's rim.
    const QColor dot = statusColor(index.data(ChatListModel::StatusRole).toString());
    const QRect &dotRect = g.dot;
    p->setPen(QPen(opt.palette.window().color(), 2));
    p->setBrush(dot.isValid() ? dot : opt.palette.window().color());
    p->drawEllipse(dotRect);

    if (!dot.isValid()) { // hollow ring for "off"
        p->setPen(QPen(opt.palette.mid().color(), 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(dotRect.adjusted(2, 2, -2, -2));
    }
}

// Small pill naming the Claude account, drawn left of the timestamp — only
// for chats in a non-default account. Returns the width it took.
static QFont pillFont(const QStyleOptionViewItem &opt) {
    QFont f = opt.font;
    f.setPointSizeF(opt.font.pointSizeF() * 0.78);
    return f;
}

// Where the account pill sits — shared by painting and tooltip hit-testing.
static QRect zonePillRect(const QStyleOptionViewItem &opt, const QRect &line, int timeW,
                          const QString &zone) {
    if (zone.isEmpty())
        return {};

    const QFontMetrics fm(pillFont(opt));
    const int w = fm.horizontalAdvance(fm.elidedText(zone, Qt::ElideRight, 80)) + 12;
    const int h = fm.height() + 2;

    return QRect(line.right() - timeW - w - 4, line.center().y() - h / 2, w, h);
}

static int paintZonePill(QPainter *p, const QStyleOptionViewItem &opt, const QRect &line,
                         int timeW, const QString &zone) {
    const QRect pill = zonePillRect(opt, line, timeW, zone);

    if (pill.isNull())
        return 0;

    const QFont f = pillFont(opt);
    const QString text = QFontMetrics(f).elidedText(zone, Qt::ElideRight, 80);
    const int w = pill.width();
    const int h = pill.height();
    QColor fill = opt.palette.highlight().color();
    fill.setAlpha(60);
    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawRoundedRect(pill, h / 2.0, h / 2.0);
    p->setFont(f);
    p->setPen(opt.palette.text().color());
    p->drawText(pill, Qt::AlignCenter, text);

    return w + 8;
}

static void paintTitleLine(QPainter *p, const QStyleOptionViewItem &opt, const QRect &line1,
                           const QModelIndex &index, bool unread) {

    QFont titleFont = opt.font;
    titleFont.setBold(true);
    p->setFont(titleFont);
    p->setPen(opt.palette.text().color());
    const QString time = index.data(ChatListModel::TimeRole).toString();
    const int timeW = opt.fontMetrics.horizontalAdvance(time) + 6;
    const int zoneW = paintZonePill(p, opt, line1, timeW, index.data(ChatListModel::ZoneRole).toString());
    p->setFont(titleFont);
    p->setPen(opt.palette.text().color());
    const QString title = QFontMetrics(titleFont).elidedText(
        index.data(ChatListModel::TitleRole).toString(), Qt::ElideRight,
        line1.width() - timeW - zoneW);
    p->drawText(line1, Qt::AlignLeft | Qt::AlignVCenter, title);

    QFont timeFont = opt.font;
    timeFont.setPointSizeF(opt.font.pointSizeF() * 0.85);
    p->setFont(timeFont);
    p->setPen(unread ? QColor("#3fa34d") : opt.palette.placeholderText().color());
    p->drawText(line1, Qt::AlignRight | Qt::AlignVCenter, time);
}

static void paintPreviewLine(QPainter *p, const QStyleOptionViewItem &opt, const QRect &r,
                             const QRect &line2, const QModelIndex &index, bool unread) {

    const QString status = index.data(ChatListModel::StatusRole).toString();
    QFont prevFont = opt.font;
    prevFont.setPointSizeF(opt.font.pointSizeF() * 0.9);
    prevFont.setBold(unread);
    prevFont.setItalic(status == "busy");
    p->setFont(prevFont);
    p->setPen(status == "frozen" ? QColor("#d64545")
              : status == "busy"   ? QColor("#3fa34d")
              : status == "idle"   ? QColor("#e6a817")
                                   : opt.palette.placeholderText().color());
    int prevW = line2.width();

    if (unread)
        prevW -= 14;

    const QString preview = QFontMetrics(prevFont).elidedText(
        index.data(ChatListModel::PreviewRole).toString(), Qt::ElideRight, prevW);
    p->drawText(line2, Qt::AlignLeft | Qt::AlignVCenter, preview);

    if (unread) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#3fa34d"));
        p->drawEllipse(QRect(r.right() - 14, line2.center().y() - 4, 9, 9));
    }
}

void ChatDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                         const QModelIndex &index) const {

    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    const RowGeometry g = rowGeometry(opt, index);
    paintBackground(p, opt, g.row);
    paintAvatar(p, opt, g, index);

    const bool unread = index.data(ChatListModel::UnreadRole).toBool();
    paintTitleLine(p, opt, g.line1, index, unread);
    paintPreviewLine(p, opt, g.row, g.line2, index, unread);

    p->restore();
}

static QString statusMeaning(const QString &status) {
    if (status == "busy")
        return QObject::tr("Working right now.");

    if (status == "live")
        return QObject::tr("Open in a Rezoom pane. This agent doesn't report working or "
                           "waiting, so the dot just means its terminal is up.");

    if (status == "idle")
        return QObject::tr("Finished and waiting for your reply.");

    if (status == "shell")
        return QObject::tr("At a shell prompt: the agent isn't running in it right now.");

    if (status == "frozen")
        return QObject::tr("Stopped by a usage limit. It can continue once the limit resets.");

    return QObject::tr("Not running. Click the chat to resume it.");
}

// What the part under the mouse means; empty = use the row's hover card.
static QString partTooltip(const QStyleOptionViewItem &opt, const QModelIndex &index,
                           const QPoint &pos) {
    const RowGeometry g = rowGeometry(opt, index);
    const QString zone = index.data(ChatListModel::ZoneRole).toString();

    if (g.dot.adjusted(-3, -3, 3, 3).contains(pos))
        return statusMeaning(index.data(ChatListModel::StatusRole).toString());

    if (index.data(ChatListModel::UnreadRole).toBool() && g.unread.adjusted(-4, -4, 4, 4).contains(pos))
        return QObject::tr("Unread: this session finished something while you were in "
                           "another chat. Opening it clears the mark.");

    if (!g.pill.isNull() && g.pill.contains(pos))
        return QObject::tr("Runs under the Claude account \"%1\" and always resumes "
                           "with that account's login.").arg(zone);

    if (g.avatar.contains(pos))
        return QObject::tr("A name tag: the title's initials. The color only tells chats "
                           "apart; the small dot on its edge is the status.");

    return {};
}

bool ChatDelegate::helpEvent(QHelpEvent *e, QAbstractItemView *view,
                             const QStyleOptionViewItem &opt, const QModelIndex &index) {
    if (e->type() == QEvent::ToolTip) {
        const QString tip = partTooltip(opt, index, e->pos());

        if (!tip.isEmpty()) {
            QToolTip::showText(e->globalPos(), tip, view);
            return true;
        }
    }

    return QStyledItemDelegate::helpEvent(e, view, opt, index);
}
