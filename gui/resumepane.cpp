#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "resumepane.h"

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

    launch = new QPushButton(this);
    launch->setMinimumHeight(40);
    connect(launch, &QPushButton::clicked, this, &ResumePane::launchRequested);
    outer->addLayout(centered(launch));

    raiseBtn = new QPushButton(tr("Raise external window"), this);
    connect(raiseBtn, &QPushButton::clicked, this,
            [this] { emit raiseRequested(externalPID); });
    outer->addLayout(centered(raiseBtn));

    note = new QLabel(this);
    note->setAlignment(Qt::AlignHCenter);
    note->setStyleSheet("color: palette(placeholder-text);");
    outer->addWidget(note);
    outer->addStretch(3);
}

void ResumePane::setChat(const Chat &c, const QString &resolvedCommand, int pid) {
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
        parts << c.cwd;

    if (!c.claudeSessionID.isEmpty())
        parts << c.claudeSessionID.left(8);

    // \xe2\x80\xa2 = UTF-8 for "•" (bullet)
    info->setText(parts.join(QStringLiteral("  \xe2\x80\xa2  ")));
    command->setText(resolvedCommand.isEmpty() ? tr("(plain shell)") : resolvedCommand);

    const bool external = pid > 0;
    raiseBtn->setVisible(external);
    launch->setEnabled(!external);

    if (external) {
        launch->setText(tr("Running outside Rezoom (pid %1)").arg(pid));

        // \xe2\x80\x94 = UTF-8 for "—" (em dash)
        note->setText(tr("Resuming here would conflict \xe2\x80\x94 raise its window instead."));
    } else if (c.kind == "ssh") {
        launch->setText(tr("Connect: %1").arg(resolvedCommand.left(60)));
        note->setText(tr("Nothing connects until you press this."));
    } else {
        // \xe2\x96\xb6 = UTF-8 for "▶" (play)
        launch->setText(tr("\xe2\x96\xb6  Rezoom"));
        note->clear();
    }
}
