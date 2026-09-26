#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

#include "core/chat.h"
#include "core/zones.h"

#include "zonedialog.h"

static const char *newEntry = "__new__";

ZoneDialog::ZoneDialog(const QString &currentZone, bool hasHistory, QWidget *parent)
    : QDialog(parent), current(currentZone), history(hasHistory) {

    setWindowTitle(tr("Run under another Claude account"));
    auto *layout = new QVBoxLayout(this);

    pick = new QComboBox(this);

    for (const Zones::Zone &z : Zones::all())
        pick->addItem(z.name.isEmpty() ? tr("Default account (%1)").arg(Chat::tildify(z.dir))
                                       : tr("%1 (%2)").arg(z.name, Chat::tildify(z.dir)),
                      z.name);

    pick->addItem(tr("New account\xe2\x80\xa6"), QLatin1String(newEntry)); // "…"
    pick->setCurrentIndex(qMax(0, pick->findData(current)));
    layout->addWidget(pick);

    newName = new QLineEdit(this);

    // \xe2\x80\x94 = UTF-8 for "—" (em dash)
    newName->setPlaceholderText(tr("Name, e.g. personal \xe2\x80\x94 creates ~/.claude-personal"));
    layout->addWidget(newName);

    share = new QCheckBox(tr("Share my settings, CLAUDE.md, rules and skills with it"), this);
    share->setChecked(true);
    layout->addWidget(share);

    explain = new QLabel(this);
    explain->setWordWrap(true);
    explain->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(explain);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ZoneDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(pick, &QComboBox::currentIndexChanged, this, &ZoneDialog::updateState);
    updateState();
}

void ZoneDialog::updateState() {
    const bool creating = pick->currentData().toString() == QLatin1String(newEntry);
    newName->setVisible(creating);
    share->setVisible(creating);

    QString text = history
        ? tr("This conversation's history lives in its current account, so it stays there. "
             "A new chat in the same folder starts under the account you pick.")
        : tr("This chat will start under the account you pick.");

    if (creating)
        text += tr(" The first time, run /login inside claude to sign that account in.");

    explain->setText(text);
}

void ZoneDialog::accept() {
    QString name = pick->currentData().toString();

    if (name == QLatin1String(newEntry)) {
        QString error;
        name = newName->text().trimmed();

        if (!Zones::add(name, share->isChecked(), &error)) {
            QMessageBox::warning(this, windowTitle(), error);
            return;
        }
    }

    chosen = name;
    QDialog::accept();
}
