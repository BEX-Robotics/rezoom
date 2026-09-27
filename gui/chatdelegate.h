#pragma once
#include <QStyledItemDelegate>

// WhatsApp-style chat row: tinted avatar with monogram + presence dot,
// bold title with timestamp, one-line preview, unread marker.
class ChatDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Per-part tooltips (status dot, unread mark, account pill, avatar);
    // anywhere else falls through to the row's hover card.
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;
};
