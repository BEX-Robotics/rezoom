#include <QPainter>

#include "chatlistmodel.h"

#include "chatdelegate.h"

static QColor statusColor(const QString &status) {
    if (status == "frozen")
        return QColor("#d64545"); // red: limit-frozen, waiting for reset

    if (status == "busy")
        return QColor("#3fa34d"); // green: claude is working

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
        sel.setAlpha(70);
        p->setPen(Qt::NoPen);
        p->setBrush(sel);
        p->drawRoundedRect(r, 8, 8);
    } else if (opt.state & QStyle::State_MouseOver) {
        QColor hov = opt.palette.text().color();
        hov.setAlpha(14);
        p->setPen(Qt::NoPen);
        p->setBrush(hov);
        p->drawRoundedRect(r, 8, 8);
    }
}

static QRect paintAvatar(QPainter *p, const QStyleOptionViewItem &opt, const QRect &r,
                         const QModelIndex &index) {

    const int d = 40;
    const QRect avatar(r.left() + 6, r.center().y() - d / 2, d, d);
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
    const QRect dotRect(avatar.right() - 11, avatar.bottom() - 11, 12, 12);
    p->setPen(QPen(opt.palette.window().color(), 2));
    p->setBrush(dot.isValid() ? dot : opt.palette.window().color());
    p->drawEllipse(dotRect);

    if (!dot.isValid()) { // hollow ring for "off"
        p->setPen(QPen(opt.palette.mid().color(), 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(dotRect.adjusted(2, 2, -2, -2));
    }

    return avatar;
}

static void paintTitleLine(QPainter *p, const QStyleOptionViewItem &opt, const QRect &line1,
                           const QModelIndex &index, bool unread) {

    QFont titleFont = opt.font;
    titleFont.setBold(true);
    p->setFont(titleFont);
    p->setPen(opt.palette.text().color());
    const QString time = index.data(ChatListModel::TimeRole).toString();
    const int timeW = opt.fontMetrics.horizontalAdvance(time) + 6;
    const QString title = QFontMetrics(titleFont).elidedText(
        index.data(ChatListModel::TitleRole).toString(), Qt::ElideRight, line1.width() - timeW);
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

    const QRect r = opt.rect.adjusted(6, 3, -6, -3);
    paintBackground(p, opt, r);
    const QRect avatar = paintAvatar(p, opt, r, index);

    const bool unread = index.data(ChatListModel::UnreadRole).toBool();
    const int textLeft = avatar.right() + 10;
    const QRect line1(textLeft, r.top() + 6, r.right() - textLeft - 4, r.height() / 2 - 6);
    const QRect line2(textLeft, r.center().y(), r.right() - textLeft - 4, r.height() / 2 - 4);
    paintTitleLine(p, opt, line1, index, unread);
    paintPreviewLine(p, opt, r, line2, index, unread);

    p->restore();
}
