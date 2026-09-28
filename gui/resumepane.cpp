#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/reptyr.h"

#include "resumepane.h"
#include "windowraiser.h"

static QHBoxLayout *centered(QWidget *w, int stretch = 2) {
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(w, stretch);
    row->addStretch(1);

    return row;
}

ResumePane::ResumePane(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->addStretch(2);

    avatar = new QLabel(this);
    avatar->setFixedSize(72, 72);
    avatar->setAlignment(Qt::AlignCenter);
    outer->addWidget(avatar, 0, Qt::AlignHCenter);

    title = new QLabel(this);
    title->setAlignment(Qt::AlignHCenter);
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.5);
    tf.setBold(true);
    title->setFont(tf);
    outer->addWidget(title);

    info = new QLabel(this);
    info->setAlignment(Qt::AlignHCenter);
    info->setStyleSheet("color: palette(placeholder-text);");
    info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    outer->addWidget(info);
    outer->addSpacing(16);

    command = new QLabel(this);
    command->setAlignment(Qt::AlignHCenter);
    command->setWordWrap(true);
    command->setTextInteractionFlags(Qt::TextSelectableByMouse);
    command->setStyleSheet(
        "QLabel { font-family: monospace; background: palette(base); "
        "border: 1px solid palette(mid); border-radius: 6px; padding: 10px; }");
    outer->addLayout(centered(command, 4));
    outer->addSpacing(12);

    addActionButtons(outer);

    addTidyRow(outer);

    note = new QLabel(this);
    note->setAlignment(Qt::AlignHCenter);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse); // pids etc. are copyable
    note->setStyleSheet("color: palette(placeholder-text);");
    outer->addWidget(note);
    outer->addStretch(3);
}

// The big action button plus its situational companions.
void ResumePane::addActionButtons(QVBoxLayout *outer) {
    launch = new QPushButton(this);
    launch->setMinimumHeight(40);
    launch->setToolTip(QStringLiteral("Ctrl+Shift+Return"));
    connect(launch, &QPushButton::clicked, this, [this] {
        if (mode == Mode::Beam)
            emit pullRequested();
        else if (mode == Mode::Continue)
            emit continueRequested(externalPID);
        else
            emit launchRequested();
    });
    outer->addLayout(centered(launch));

    resumeHereBtn = new QPushButton(tr("Resume in Rezoom instead"), this);
    resumeHereBtn->setMinimumHeight(40);
    resumeHereBtn->setToolTip(tr("Ends the frozen process and resumes the conversation "
                                 "here from its transcript"));
    connect(resumeHereBtn, &QPushButton::clicked, this,
            [this] { emit resumeHereRequested(externalPID); });
    outer->addLayout(centered(resumeHereBtn));

    raiseBtn = new QPushButton(tr("Go to its window"), this);
    raiseBtn->setMinimumHeight(40);
    connect(raiseBtn, &QPushButton::clicked, this,
            [this] { emit raiseRequested(externalPID); });
    outer->addLayout(centered(raiseBtn));
}

// Done with it? A not-running chat can be filed away or forgotten here.
void ResumePane::addTidyRow(QVBoxLayout *outer) {
    auto *tidy = new QHBoxLayout;
    archiveBtn = new QPushButton(this);
    archiveBtn->setToolTip(QStringLiteral("Ctrl+Shift+E"));
    connect(archiveBtn, &QPushButton::clicked, this, &ResumePane::archiveRequested);
    forgetBtn = new QPushButton(tr("Forget\xe2\x80\xa6"), this); // "…"
    forgetBtn->setToolTip(tr("Remove from Rezoom; the transcript stays on disk (Ctrl+Shift+Delete)"));
    connect(forgetBtn, &QPushButton::clicked, this, &ResumePane::forgetRequested);
    tidy->addStretch(1);
    tidy->addWidget(archiveBtn);
    tidy->addWidget(forgetBtn);
    tidy->addStretch(1);
    outer->addLayout(tidy);
}

void ResumePane::setChat(const Chat &c, const QString &resolvedCommand, int pid,
                         const SessionHealth::Health &health) {
    externalPID = pid;

    avatar->setText(c.monogram());
    avatar->setStyleSheet(QStringLiteral("QLabel { background: %1; color: white; "
                                         "border-radius: 36px; font-size: 28px; "
                                         "font-weight: bold; }")
                              .arg(Chat::tintColorHex(c.tint)));
    title->setText(c.title.isEmpty() ? tr("(untitled)") : c.title);

    QStringList parts;
    parts << c.kind;

    if (!c.cwd.isEmpty())
        parts << Chat::tildify(c.cwd);

    if (!c.zone.isEmpty())
        parts << tr("account: %1").arg(c.zone);

    if (!c.claudeSessionID.isEmpty())
        parts << c.claudeSessionID.left(8);

    // \xe2\x80\xa2 = UTF-8 for "•" (bullet)
    info->setText(parts.join(QString::fromUtf8("  \xe2\x80\xa2  ")));
    command->setText(resolvedCommand.isEmpty() ? tr("(plain shell)") : resolvedCommand);

    setActions(c, resolvedCommand, pid, health);
}

// The big button and its companions, per situation: not running, running
// elsewhere (beam / go to window), or suspended there (continue / resume here).
void ResumePane::setActions(const Chat &c, const QString &cmd, int pid,
                            const SessionHealth::Health &health) {
    const bool external = pid > 0;
    const bool suspended = external && health.state == SessionHealth::State::Suspended;
    const bool canRaise = external && WindowRaiser::canRaise(pid);
    archiveBtn->setText(c.archived ? tr("Unarchive") : tr("Archive"));
    archiveBtn->setVisible(!external); // only for chats that aren't running
    forgetBtn->setVisible(!external);
    raiseBtn->setVisible(canRaise && !suspended);
    resumeHereBtn->setVisible(suspended);
    launch->setVisible(true);
    launch->setEnabled(true);
    mode = Mode::Launch;

    if (suspended)
        setSuspendedActions(pid, health);
    else if (external)
        setExternalActions(pid, canRaise);
    else if (c.kind == "ssh") {
        launch->setText(tr("Connect: %1").arg(cmd.left(60)));
        note->setText(tr("Nothing connects until you press this."));
    } else {
        // \xe2\x96\xb6 = UTF-8 for "▶" (play)
        launch->setText(tr("\xe2\x96\xb6  Rezoom"));
        note->clear();
    }
}

void ResumePane::setSuspendedActions(int pid, const SessionHealth::Health &health) {
    const bool canContinue = WindowRaiser::canContinue(pid);
    mode = Mode::Continue;
    launch->setVisible(canContinue);

    // \xe2\x96\xb6 = UTF-8 for "▶" (play), \xe2\x80\x94 = "—" (em dash)
    launch->setText(tr("\xe2\x96\xb6  Continue"));
    note->setText(canContinue
        ? tr("Suspended (Ctrl+Z) in its terminal for %1 \xe2\x80\x94 continue it there, or "
             "resume the conversation here.").arg(SessionHealth::ageText(health.sinceMs))
        : tr("Suspended (Ctrl+Z) for %1 (pid %2). Rezoom can't wake it in its own "
             "terminal (no Konsole shell prompt to type into), so resume the "
             "conversation here instead.")
              .arg(SessionHealth::ageText(health.sinceMs)).arg(pid));
}

void ResumePane::setExternalActions(int pid, bool canRaise) {
    if (Reptyr::supported()) {
        mode = Mode::Beam;

        // \xe2\xa4\xb5 = UTF-8 for "⤵", \xe2\x80\x94 = "—" (em dash)
        launch->setText(tr("\xe2\xa4\xb5  Beam it in"));
        note->setText(canRaise ? tr("Running outside Rezoom (pid %1) \xe2\x80\x94 beam it into "
                                    "an embedded pane, or go to its window.").arg(pid)
                               : tr("Running outside Rezoom (pid %1) \xe2\x80\x94 beam it into "
                                    "an embedded pane.").arg(pid));

        return;
    }

    launch->setEnabled(false);
    note->setText(canRaise ? tr("Running outside Rezoom (pid %1) \xe2\x80\x94 go to its "
                                "window.").arg(pid)
                           : tr("Running outside Rezoom (pid %1).").arg(pid));
}
