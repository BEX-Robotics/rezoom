#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <QVBoxLayout>
#include <kde_terminal_interface.h>

#include "core/liveregistry.h"
#include "core/processscout.h"

#include "terminalpane.h"

TerminalPane::TerminalPane(const QString &chatID, const QString &profile, QWidget *parent)
    : QWidget(parent), id(chatID) {

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

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

    // Type-ahead into the fresh shell; zsh replays it at the first prompt.
    const QString cmd = command;
    QTimer::singleShot(250, this, [this, cmd] {
        if (term)
            term->sendInput(cmd + QStringLiteral("\n"));
    });
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

    const auto procs = ProcessScout::findDescendants(shell, {"claude", "ssh", "tmux: client"});

    for (const auto &p : procs) {
        if (p.comm == "claude") {
            if (p.pid != lastClaudePID) {
                lastClaudePID = p.pid;
                emit childClaude(id, p.pid);
            }
        } else if (p.comm == "ssh" && !reportedSsh) {
            reportedSsh = true;
            emit childSsh(id, p.cmdline);
        } else if (p.comm.startsWith("tmux") && !reportedTmux) {
            reportedTmux = true;
            emit childTmux(id, p.cmdline);
        }
    }
}
