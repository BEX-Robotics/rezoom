#pragma once
#include <QDialog>

#include "core/chat.h"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class SessionStore;
class LiveRegistry;

// "Suck in" sessions living outside Rezoom: running claudes, historical
// transcripts, tmux sessions and ssh clients. Everything here is a local
// read — adopting records metadata, it never launches or connects.
class AdoptDialog : public QDialog {
    Q_OBJECT
public:
    AdoptDialog(SessionStore *store, LiveRegistry *registry, QWidget *parent = 0);

private:
    QWidget *makeRunningTab();
    QWidget *makeHistoryTab();
    QWidget *makeTerminalsTab();
    QWidget *wrapTab(QTreeWidget *tree, const QString &hint, QLineEdit *search = 0);
    void adoptSelected(QTreeWidget *tree);
    void adoptAll(QTreeWidget *tree);
    void adoptItem(QTreeWidgetItem *item);
    bool tracked(const QString &sessionID) const;

    SessionStore *store = 0;
    LiveRegistry *registry = 0;
    int adopted = 0;
};
