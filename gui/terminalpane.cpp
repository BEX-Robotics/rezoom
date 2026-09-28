#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <kde_terminal_interface.h>

#include "core/liveregistry.h"
#include "core/processscout.h"

#include "terminalpane.h"

TerminalPane::TerminalPane(const QString &chatID, const QString &profile, QWidget *parent)
    : QWidget(parent), id(chatID) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    buildSshBanner(layout);

    const auto result =
        KPluginFactory::loadFactory(KPluginMetaData(QStringLiteral("kf6/parts/konsolepart")));

    if (!result) {
        error = result.errorText;
        return;
    }

    part = result.plugin->create<KParts::ReadOnlyPart>(this, this);

    if (!part) {
        error = QStringLiteral("konsolepart factory returned no part");
        return;
    }

    term = qobject_cast<TerminalInterface *>(part);

    if (!term) {
        error = QStringLiteral("konsolepart does not expose TerminalInterface");
        part->deleteLater();
        part = 0;

        return;
    }

    layout->addWidget(part->widget());

    if (!profile.isEmpty() && term->availableProfiles().contains(profile))
        term->setCurrentProfile(profile);

    // Konsole keeps its caption on claude's current activity — live title.
    connect(part, &KParts::ReadOnlyPart::setWindowCaption, this,
            [this](const QString &caption) {
                // Before claude sets a title Konsole shows the profile name
                // ("tint-yellow") — that's not a title.
                const QString c = caption.trimmed();
                emit captionChanged(id, term && c == term->currentProfileName() ? QString() : c);
            });

    // Konsole destroys the part when the shell exits (e.g. Ctrl-D).
    connect(part, &QObject::destroyed, this, [this] {
        part = 0;
        term = 0;
        emitTerminated();
    });

    connect(&timer, &QTimer::timeout, this, &TerminalPane::poll);
    timer.start(1500);
}

void TerminalPane::emitTerminated() {
    if (terminatedEmitted)
        return;

    terminatedEmitted = true;
    emit terminated(id);
}

bool TerminalPane::shellAlive() const {
    return term && shell > 0 && LiveRegistry::pidAlive(shell);
}

void TerminalPane::runCommand(const QString &cwd, const QString &command) {
    if (!term)
        return;

    term->showShellInDir(cwd);
    shell = term->terminalProcessId();

    if (command.trimmed().isEmpty())
        return;

    // Type-ahead into the fresh shell; it replays at the first prompt.
    const QString cmd = command;
    QTimer::singleShot(250, this, [this, cmd] {
        if (term)
            term->sendInput(cmd + QStringLiteral("\n"));
    });
}

void TerminalPane::typeCommand(const QString &command) {
    if (term)
        term->sendInput(command + QStringLiteral("\n"));
}

void TerminalPane::showEvent(QShowEvent *ev) {
    QWidget::showEvent(ev);

    if (part && part->widget())
        part->widget()->setFocus();
}

void TerminalPane::poll() {
    if (!term)
        return;

    if (shell <= 0)
        shell = term->terminalProcessId();

    if (shell <= 0)
        return;

    if (!LiveRegistry::pidAlive(shell)) {
        timer.stop();
        emitTerminated();
        return;
    }

    const auto procs = ProcessScout::findDescendants(shell, {"claude", "codex", "ssh", "tmux: client"});

    for (const auto &p : procs) {
        if (p.comm == "claude") {
            if (p.pid != lastClaudePID) {
                lastClaudePID = p.pid;
                emit childClaude(id, p.pid);
            }
        } else if (p.comm == "codex") {
            if (p.pid != lastCodexPID) {
                lastCodexPID = p.pid;
                emit childCodex(id, p.pid);
            }
        } else if (p.comm == "ssh") {
            // Only an ssh typed at this pane's own prompt is the chat's
            // connection; ones spawned deeper (git push, tools an agent
            // runs, nested apps) are none of its business.
            if (ProcessScout::parentPid(p.pid) != shell)
                continue;

            trackSsh(p.pid, p.cmdline);

            if (!reportedSsh) {
                reportedSsh = true;
                emit childSsh(id, p.cmdline);
            }
        } else if (p.comm.startsWith("tmux") && !reportedTmux) {
            reportedTmux = true;
            emit childTmux(id, p.cmdline);
        }
    }

    if (sshPID > 0 && !LiveRegistry::pidAlive(sshPID)) {
        sshPID = 0;
        showSshBanner();
    }
}

// A (new) ssh is running in this pane — remember exactly how it was started.
void TerminalPane::trackSsh(int pid, const QStringList &cmdline) {
    if (pid == sshPID)
        return;

    sshPID = pid;
    sshCommand = cmdline;
    hideSshBanner(); // a fresh connection supersedes an "ended" notice
}

// Rezoom can't tell a dropped connection from a typed `exit`, so this says
// "ended" and only offers — reconnecting is always the user's click.
void TerminalPane::showSshBanner() {
    const QString dest = ProcessScout::sshDestination(sshCommand);

    // \xc2\xb7 = UTF-8 for "·"
    bannerText->setText(tr("ssh to %1 ended \xc2\xb7 Ctrl+Shift+Return reconnects")
                            .arg(dest.isEmpty() ? tr("the remote host") : dest));
    banner->show();
    emit sshEnded(id, true);
}

void TerminalPane::hideSshBanner() {
    if (banner->isHidden())
        return;

    banner->hide();
    emit sshEnded(id, false);
}

bool TerminalPane::hasEndedSsh() const {
    return !banner->isHidden();
}

static QString shellQuote(const QString &arg) {
    static const QRegularExpression safe("^[A-Za-z0-9@%+=:,./_-]+$");

    if (safe.match(arg).hasMatch())
        return arg;

    QString q = arg;
    q.replace('\'', QLatin1String("'\\''"));
    return "'" + q + "'";
}

void TerminalPane::reconnectSsh() {
    QStringList parts;

    for (const QString &a : sshCommand)
        parts << shellQuote(a);

    hideSshBanner();
    typeCommand(parts.join(' '));
}

// Thin bar above the terminal: what ended, Reconnect, dismiss.
void TerminalPane::buildSshBanner(QVBoxLayout *layout) {
    banner = new QFrame(this);
    banner->setStyleSheet("QFrame { background: palette(alternate-base); "
                          "border-bottom: 1px solid palette(mid); }");
    auto *row = new QHBoxLayout(banner);
    row->setContentsMargins(10, 4, 6, 4);
    bannerText = new QLabel(banner);
    bannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(bannerText, 1);
    auto *reconnect = new QPushButton(tr("Reconnect"), banner);
    reconnect->setToolTip(QStringLiteral("Ctrl+Shift+Return"));
    connect(reconnect, &QPushButton::clicked, this, &TerminalPane::reconnectSsh);
    row->addWidget(reconnect);
    auto *dismiss = new QPushButton(QString::fromUtf8("\xe2\x9c\x95"), banner); // "✕"
    dismiss->setFlat(true);
    dismiss->setToolTip(tr("Dismiss"));
    connect(dismiss, &QPushButton::clicked, this, &TerminalPane::hideSshBanner);
    row->addWidget(dismiss);
    banner->hide();
    layout->addWidget(banner);
}
