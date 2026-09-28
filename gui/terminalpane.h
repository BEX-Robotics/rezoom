#pragma once
#include <QTimer>
#include <QWidget>

namespace KParts {
class ReadOnlyPart;
}

class TerminalInterface;
class QFrame;
class QLabel;
class QPushButton;
class QVBoxLayout;

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

    // Type a command into the pane's existing shell (no new shell).
    void typeCommand(const QString &command);

    // An ssh that ran in this pane has exited and its banner is showing.
    bool hasEndedSsh() const;
    void reconnectSsh(); // retype exactly the command that ended

    // Is claude/codex actually running in this pane (vs. an idle shell)?
    bool hasAgent() const;
    void showExternalBanner(int pid);
    void hideExternalBanner();
    bool hasExternalBanner() const { return bannerMode == BannerMode::External; }

signals:
    void terminated(const QString &chatID);
    void childClaude(const QString &chatID, int claudePID);
    void childCodex(const QString &chatID, int codexPID);
    void captionChanged(const QString &chatID, const QString &caption);
    void sshEnded(const QString &chatID, bool ended); // banner shown / cleared
    void beamHereRequested(const QString &chatID, int externalPID);
    void childSsh(const QString &chatID, const QStringList &cmdline);
    void childTmux(const QString &chatID, const QStringList &cmdline);

protected:
    void showEvent(QShowEvent *ev) override;

private slots:
    void poll();

private:
    void emitTerminated();
    void trackSsh(int pid, const QStringList &cmdline);
    void buildSshBanner(QVBoxLayout *layout);
    void showSshBanner();
    void hideSshBanner();
    void bannerAction();

    QString id;
    QString error;
    KParts::ReadOnlyPart *part = 0;
    TerminalInterface *term = 0;
    QTimer timer;
    int shell = 0;
    int lastClaudePID = 0;
    int sshPID = 0;
    QStringList sshCommand;
    enum class BannerMode { None, SshEnded, External };

    QFrame *banner = 0;
    QPushButton *bannerButton = 0;
    BannerMode bannerMode = BannerMode::None;
    int externalPID = 0;
    QLabel *bannerText = 0;
    int lastCodexPID = 0;
    bool reportedSsh = false;
    bool reportedTmux = false;
    bool terminatedEmitted = false;
};
