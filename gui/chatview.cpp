#include "chatview.h"
#include "resumepane.h"
#include "terminalpane.h"

ChatView::ChatView(const QString &chatID, QWidget *parent)
    : QStackedWidget(parent), id(chatID) {

    resumePane = new ResumePane(this);
    addWidget(resumePane);
}

void ChatView::attachPane(TerminalPane *p) {
    pane = p;
    addWidget(p);
    setCurrentWidget(p);
}

void ChatView::paneGone() {
    if (pane) {
        removeWidget(pane);
        pane = 0;
    }

    setCurrentWidget(resumePane);
}
