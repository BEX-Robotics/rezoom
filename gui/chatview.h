#pragma once
#include <QStackedWidget>

class ResumePane;
class TerminalPane;

// Per-chat container: shows the chat's ResumePane when idle and its
// TerminalPane while running. Moves freely between the main window's
// stack and floating group windows.
class ChatView : public QStackedWidget {
    Q_OBJECT
public:
    explicit ChatView(const QString &chatID, QWidget *parent = 0);

    QString chatID() const { return id; }
    ResumePane *resume() { return resumePane; }
    TerminalPane *terminal() { return pane; }

    void attachPane(TerminalPane *p);
    void paneGone(); // terminal died — back to the resume pane

private:
    QString id;
    ResumePane *resumePane = 0;
    TerminalPane *pane = 0;
};
