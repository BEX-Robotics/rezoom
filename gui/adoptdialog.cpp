#include <QDateTime>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/liveregistry.h"
#include "core/processscout.h"
#include "core/sessionstore.h"
#include "core/transcriptindex.h"

#include "adoptdialog.h"

// The prospective Chat rides on the item as serialized json.
static const int chatRole = Qt::UserRole + 1;

static void attachChat(QTreeWidgetItem *item, const Chat &c) {
    item->setData(0, chatRole, c.toJson());
}

AdoptDialog::AdoptDialog(SessionStore *store, LiveRegistry *registry, QWidget *parent)
    : QDialog(parent), store(store), registry(registry) {

    setWindowTitle(tr("Adopt sessions"));
    resize(780, 480);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(makeRunningTab(), tr("Running claude"));
    tabs->addTab(makeHistoryTab(), tr("History"));
    tabs->addTab(makeTerminalsTab(), tr("Terminals"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

bool AdoptDialog::tracked(const QString &sessionID) const {
    return store->findByClaudeSession(sessionID) != 0;
}

QWidget *AdoptDialog::wrapTab(QTreeWidget *tree, const QString &hint, QLineEdit *search) {
    auto *w = new QWidget(this);
    auto *layout = new QVBoxLayout(w);

    if (search)
        layout->addWidget(search);

    layout->addWidget(tree);

    auto *row = new QHBoxLayout;
    auto *hintLabel = new QLabel(hint, w);
    hintLabel->setStyleSheet("color: palette(placeholder-text);");
    row->addWidget(hintLabel, 1);
    auto *adoptBtn = new QPushButton(tr("Adopt selected"), w);
    auto *allBtn = new QPushButton(tr("Adopt all"), w);
    connect(adoptBtn, &QPushButton::clicked, this, [this, tree] { adoptSelected(tree); });
    connect(allBtn, &QPushButton::clicked, this, [this, tree] { adoptAll(tree); });
    row->addWidget(adoptBtn);
    row->addWidget(allBtn);
    layout->addLayout(row);

    return w;
}

QWidget *AdoptDialog::makeRunningTab() {
    auto *tree = new QTreeWidget(this);
    tree->setHeaderLabels({tr("Name"), tr("Status"), tr("Directory"), tr("Session")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    for (const LiveEntry &e : registry->bySessionID()) {
        if (tracked(e.sessionID))
            continue;

        Chat c = Chat::create("claude");
        c.claudeSessionID = e.sessionID;
        c.cwd = e.cwd;
        c.lastActiveAt = e.updatedAt;
        const QString first = TranscriptIndex::previewForSession(e.sessionID);
        const bool derived = e.nameSource == "derived" || e.name.isEmpty();
        c.title = (derived && !first.isEmpty()) ? first.left(40) : e.name;
        c.preview = TranscriptIndex::lastMessagePreview(e.sessionID);

        if (c.preview.isEmpty())
            c.preview = first;

        auto *item = new QTreeWidgetItem(
            tree, {c.title, e.status, e.cwd, e.sessionID.left(8)});
        attachChat(item, c);
    }

    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    return wrapTab(tree, tr("Claude sessions running outside Rezoom right now."));
}

QWidget *AdoptDialog::makeHistoryTab() {
    auto *tree = new QTreeWidget(this);
    tree->setHeaderLabels({tr("Last active"), tr("Directory"), tr("First message")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    const QList<TranscriptInfo> all = TranscriptIndex::scanAll();

    for (const TranscriptInfo &t : all) {
        if (tracked(t.sessionID) || t.preview.isEmpty())
            continue;

        Chat c = Chat::create("claude");
        c.claudeSessionID = t.sessionID;
        c.cwd = t.cwd;
        c.preview = t.preview;
        c.title = t.preview.left(40);
        c.lastActiveAt = t.mtimeMs;
        const QString when = QDateTime::fromMSecsSinceEpoch(t.mtimeMs).toString("yyyy-MM-dd HH:mm");
        auto *item = new QTreeWidgetItem(tree, {when, t.cwd, t.preview.left(90)});
        attachChat(item, c);
    }

    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    auto *search = new QLineEdit(this);
    search->setPlaceholderText(tr("Filter\xe2\x80\xa6")); // \xe2\x80\xa6 = UTF-8 for "…" (ellipsis)
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [tree](const QString &text) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = tree->topLevelItem(i);
            const bool hit = text.isEmpty() || item->text(1).contains(text, Qt::CaseInsensitive)
                || item->text(2).contains(text, Qt::CaseInsensitive);
            item->setHidden(!hit);
        }
    });

    return wrapTab(tree, tr("Every claude transcript on this machine, newest first."), search);
}

QWidget *AdoptDialog::makeTerminalsTab() {
    auto *tree = new QTreeWidget(this);
    tree->setHeaderLabels({tr("Type"), tr("What"), tr("Details")});
    tree->setRootIsDecorated(false);
    tree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    for (const auto &t : ProcessScout::tmuxSessions()) {
        Chat c = Chat::create("tmux");
        c.tmuxSession = t.name;
        c.title = "tmux: " + t.name;
        auto *item = new QTreeWidgetItem(
            tree, {"tmux", t.name, t.attached ? tr("attached") : tr("detached")});
        attachChat(item, c);
    }

    for (const auto &s : ProcessScout::runningSsh()) {
        Chat c = Chat::create("ssh");
        c.entryCommand = s.cmdline.join(' ');
        c.host = ProcessScout::sshDestination(s.cmdline);
        c.title = c.host.isEmpty() ? c.entryCommand.left(40) : c.host;
        auto *item = new QTreeWidgetItem(
            tree, {"ssh", c.host, c.entryCommand});
        attachChat(item, c);
    }

    tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);

    return wrapTab(tree, tr("Local tmux sessions and live ssh connections."));
}

void AdoptDialog::adoptItem(QTreeWidgetItem *item) {
    const QJsonObject o = item->data(0, chatRole).toJsonObject();

    if (o.isEmpty())
        return;

    store->add(Chat::fromJson(o));
    ++adopted;
    delete item;
}

void AdoptDialog::adoptSelected(QTreeWidget *tree) {
    const QList<QTreeWidgetItem *> sel = tree->selectedItems();

    for (QTreeWidgetItem *item : sel)
        adoptItem(item);
}

void AdoptDialog::adoptAll(QTreeWidget *tree) {
    while (tree->topLevelItemCount())
        adoptItem(tree->topLevelItem(0));
}
