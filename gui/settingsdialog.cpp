#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/hookinstaller.h"
#include "core/templates.h"

#include "settingsdialog.h"

SettingsDialog::SettingsDialog(Templates *templates, QWidget *parent)
    : QDialog(parent), templates(templates) {

    setWindowTitle(tr("Rezoom settings"));
    resize(640, 420);

    auto *layout = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Resumable-command templates. Placeholders: {session_id} {cwd} {host} "
           "{tmux_session} {entry_command} {title}. Also editable in "
           "~/.config/rezoom/rezoom.conf."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    table = new QTableWidget(this);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({tr("Name"), tr("Command")});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);

    const auto entries = templates->all();
    table->setRowCount(entries.size());

    for (int i = 0; i < entries.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(entries[i].name));
        table->setItem(i, 1, new QTableWidgetItem(entries[i].command));
    }

    layout->addWidget(table);

    auto *row = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add"), this);
    auto *delBtn = new QPushButton(tr("Remove"), this);
    connect(addBtn, &QPushButton::clicked, this, [this] {
        table->insertRow(table->rowCount());
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch(1);
    layout->addLayout(row);

    addPrefChecks(layout);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::addPrefChecks(QVBoxLayout *layout) {
    confirmClose = new QCheckBox(tr("Confirm before closing with live embedded sessions"), this);
    confirmClose->setChecked(templates->confirmClose());
    layout->addWidget(confirmClose);

    autoAdopt = new QCheckBox(tr("Auto-adopt new interactive claude sessions as chats"), this);
    autoAdopt->setChecked(templates->autoAdopt());
    layout->addWidget(autoAdopt);

    resumeOnStart = new QCheckBox(tr("On startup, resume the sessions that were running"), this);
    resumeOnStart->setChecked(templates->resumeOnStart());
    layout->addWidget(resumeOnStart);

    autoStart = new QCheckBox(tr("Start Rezoom at login"), this);
    autoStart->setChecked(templates->autoStart());
    layout->addWidget(autoStart);

    preTrust = new QCheckBox(tr("Skip Claude's \"trust this folder?\" prompt for folders "
                                "already in Rezoom"), this);
    preTrust->setToolTip(tr("Writes hasTrustDialogAccepted into ~/.claude.json for those "
                            "folders, only while no claude is running. Off by default because "
                            "it answers a security prompt on your behalf."));
    preTrust->setChecked(templates->preTrust());
    layout->addWidget(preTrust);

    addLiveMovesRow(layout);
    addFreezeRow(layout);
}

// Freeze detection = our Notification hook in ~/.claude/settings.json.
// Acts immediately (no OK needed); with no hook script there's no button.
void SettingsDialog::addFreezeRow(QVBoxLayout *layout) {
    auto *row = new QHBoxLayout;
    auto *status = new QLabel(this);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    row->addWidget(status, 1);
    auto *toggle = new QPushButton(this);
    row->addWidget(toggle);
    layout->addLayout(row);

    const auto refresh = [status, toggle] {
        const bool on = HookInstaller::installed();
        const bool available = !HookInstaller::hookPath().isEmpty();

        if (on)
            status->setText(tr("Freeze detection is on (Notification hook in "
                               "~/.claude/settings.json)."));
        else if (available)
            status->setText(tr("Freeze detection is off. Installing adds a Notification hook "
                               "to ~/.claude/settings.json (backup kept)."));
        else
            status->setText(tr("Freeze detection needs rezoom-notify-hook on your PATH; it "
                               "ships with the Rezoom packages."));

        toggle->setText(on ? tr("Remove") : tr("Install"));
        toggle->setVisible(on || available);
    };
    refresh();

    connect(toggle, &QPushButton::clicked, this, [this, refresh] {
        QString error;
        const bool ok = HookInstaller::installed() ? HookInstaller::uninstall(&error)
                                                   : HookInstaller::install(&error);

        if (!ok)
            QMessageBox::warning(this, tr("Freeze detection"), error);

        refresh();
    });
}

// reptyr live-move toggle plus its readiness, with the copyable fix command
// when it needs one. Hidden entirely where reptyr can't run (macOS).
void SettingsDialog::addLiveMovesRow(QVBoxLayout *layout) {
    if (!Reptyr::supported())
        return;

    liveMoves = new QCheckBox(tr("Move live sessions in/out with reptyr (no kill)"), this);
    liveMoves->setChecked(templates->liveMoves());
    layout->addWidget(liveMoves);

    const Reptyr::Status st = Reptyr::status();
    QString text = tr("reptyr: %1").arg(st.reason);

    if (!st.fixCommand.isEmpty())
        text += tr("  \xe2\x80\x94  fix: sudo %1").arg(st.fixCommand); // "—"

    auto *status = new QLabel(text, this);
    status->setEnabled(false);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse); // fix is copyable
    status->setStyleSheet(st.ready ? "color: palette(placeholder-text);" : "color: #d64545;");
    layout->addWidget(status);
}

void SettingsDialog::accept() {
    QList<Templates::Entry> entries;

    for (int i = 0; i < table->rowCount(); ++i) {
        const QTableWidgetItem *name = table->item(i, 0);
        const QTableWidgetItem *cmd = table->item(i, 1);

        if (name && !name->text().trimmed().isEmpty())
            entries.append({name->text().trimmed(), cmd ? cmd->text() : QString()});
    }

    templates->replaceAll(entries);
    templates->setConfirmClose(confirmClose->isChecked());
    templates->setAutoAdopt(autoAdopt->isChecked());
    templates->setResumeOnStart(resumeOnStart->isChecked());
    templates->setAutoStart(autoStart->isChecked());
    templates->setPreTrust(preTrust->isChecked());

    if (liveMoves)
        templates->setLiveMoves(liveMoves->isChecked());
    QDialog::accept();
}
