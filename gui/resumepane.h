#pragma once
#include <QWidget>

#include "core/chat.h"

class QLabel;
class QPushButton;

// Shown for a chat with no live embedded terminal: the resumable command and
// a big button to run it. For SSH chats the click IS the consent — nothing
// connects anywhere until the user pushes it.
class ResumePane : public QWidget {
    Q_OBJECT
public:
    explicit ResumePane(QWidget *parent = 0);

    // externalPID > 0 → session already running outside the app.
    void setChat(const Chat &c, const QString &resolvedCommand, int externalPID);

signals:
    void launchRequested();
    void raiseRequested(int externalPID);

private:
    QLabel *avatar = 0;
    QLabel *title = 0;
    QLabel *info = 0;
    QLabel *command = 0;
    QLabel *note = 0;
    QPushButton *launch = 0;
    QPushButton *raiseBtn = 0;
    int externalPID = 0;
};
