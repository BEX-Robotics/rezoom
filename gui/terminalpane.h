#pragma once
#include <QTimer>
#include <QWidget>

namespace KParts {
class ReadOnlyPart;
}

class TerminalInterface;

// One embedded Konsole terminal (konsolepart KPart) hosting one chat.
// Watches its shell's process tree so claude/ssh/tmux started inside are
// recognized and recorded automatically.
class TerminalPane : public QWidget {
    Q_OBJECT
public:
    TerminalPane(const QString &chatID, const QString &profile, QWidget *parent = 0);

    bool valid() const { return term != 0; }
    QString errorText() const { return error; }
    QString chatID() const { return id; }
    int shellPID() const { return shell; }

    bool shellAlive() const;

    // Open a shell in cwd and (if command non-empty) type the command in.
    void runCommand(const QString &cwd, const QString &command);

signals:
    void terminated(const QString &chatID);
    void childClaude(const QString &chatID, int claudePID);
    void childCodex(const QString &chatID, int codexPID);
    void captionChanged(const QString &chatID, const QString &caption);
    void childSsh(const QString &chatID, const QStringList &cmdline);
    void childTmux(const QString &chatID, const QStringList &cmdline);

protected:
    void showEvent(QShowEvent *ev) override;

private slots:
    void poll();

private:
    void emitTerminated();

    QString id;
    QString error;
    KParts::ReadOnlyPart *part = 0;
    TerminalInterface *term = 0;
    QTimer timer;
    int shell = 0;
    int lastClaudePID = 0;
    int lastCodexPID = 0;
    bool reportedSsh = false;
    bool reportedTmux = false;
    bool terminatedEmitted = false;
};
