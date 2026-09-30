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
    // The working spinner's frame: advanced by a timer while rows are busy.
    static void advanceSpinner();
    static QString spinnerGlyph();

    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;

    // The row's small beam-in button (sessions running in another window).
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

signals:
    void beamRequested(const QString &chatID);
};
