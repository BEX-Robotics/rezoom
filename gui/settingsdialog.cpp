#include <QCheckBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

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
    QDialog::accept();
}
