#pragma once
#include <QWidget>

#include "core/chat.h"
#include "core/sessionhealth.h"

class QLabel;
class QPushButton;
class QVBoxLayout;

// Shown for a chat with no live embedded terminal: the resumable command and
// a big button to run it. For SSH chats the click IS the consent — nothing
// connects anywhere until the user pushes it.
class ResumePane : public QWidget {
    Q_OBJECT
public:
    explicit ResumePane(QWidget *parent = 0);

    // externalPID > 0 → session already running outside the app.
    void setChat(const Chat &c, const QString &resolvedCommand, int externalPID,
                 const SessionHealth::Health &health = {});

signals:
    void launchRequested();
    void raiseRequested(int externalPID);
    void pullRequested(); // "beam it in": live pull, verified fallback
    void archiveRequested(); // toggles archived
    void forgetRequested();
    void continueRequested(int externalPID);   // suspended: fg in its terminal
    void resumeHereRequested(int externalPID); // suspended: end it, resume here

private:
    enum class Mode { Launch, Beam, Continue }; // what the big button does

    void addActionButtons(QVBoxLayout *outer);
    void addTidyRow(QVBoxLayout *outer);
    void setActions(const Chat &c, const QString &cmd, int pid,
                    const SessionHealth::Health &health);
    void setSuspendedActions(int pid, const SessionHealth::Health &health);
    void setExternalActions(int pid, bool canRaise);

    QLabel *avatar = 0;
    QLabel *title = 0;
    QLabel *info = 0;
    QLabel *command = 0;
    QLabel *note = 0;
    QPushButton *launch = 0;
    QPushButton *raiseBtn = 0;
    QPushButton *archiveBtn = 0;
    QPushButton *forgetBtn = 0;
    int externalPID = 0;
    Mode mode = Mode::Launch;
    QPushButton *resumeHereBtn = 0;
};
