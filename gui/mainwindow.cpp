#include <signal.h>

#include <algorithm>

#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#ifdef REZOOM_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/codexindex.h"
#include "core/externalterminal.h"
#include "core/processscout.h"
#include "core/transcriptindex.h"

#include "adoptdialog.h"
#include "chatdelegate.h"
#include "chatlistmodel.h"
#include "chatview.h"
#include "floatwindow.h"
#include "mainwindow.h"
#include "resumepane.h"
#include "settingsdialog.h"
#include "shortcutsdialog.h"
#include "terminalpane.h"

static void launchInKonsole(const QString &cwd, const QString &command) {
    ExternalTerminal::launch(cwd, command);
}

// Policy: any dialog text showing ids, commands or paths must be
// mouse-copyable (see CLAUDE.md). Qt's static convenience dialogs aren't —
// these wrappers are.
static QString getTextSelectable(QWidget *parent, const QString &title,
                                 const QString &label, const QString &text, bool *ok) {
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setTextValue(text);

    for (QLabel *l : dialog.findChildren<QLabel *>())
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);

    *ok = dialog.exec() == QDialog::Accepted;

    return dialog.textValue();
}

static bool askSelectable(QWidget *parent, const QString &title, const QString &text) {
    QMessageBox box(QMessageBox::Question, title, text,
                    QMessageBox::Yes | QMessageBox::No, parent);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);

    return box.exec() == QMessageBox::Yes;
}

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Rezoom"));
    resize(1200, 760);

    model = new ChatListModel(&store, &registry, &notifications, this);

    splitter = new QSplitter(this);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 900});
    setCentralWidget(splitter);

    buildShortcuts();

    // Model refreshes are full resets, which clear the view's selection —
    // quietly re-pin the current chat after every rebuild (no scroll jump).
    connect(model, &QAbstractItemModel::modelReset, this, [this] {
        if (currentID.isEmpty())
            return;

        const QModelIndex idx = model->indexOf(currentID);

        if (idx.isValid())
            list->setCurrentIndex(idx);
    });

    connect(&registry, &LiveRegistry::updated, this, &MainWindow::onRegistryUpdated);
    connect(&notifications, &NotificationWatcher::updated, this, &MainWindow::updateAttention);
    onRegistryUpdated();
    restoreUiState();

    if (store.chats().isEmpty())
        QMetaObject::invokeMethod(this, &MainWindow::openAdopt, Qt::QueuedConnection);

    fixDerivedTitlesOnce();

    if (templates.resumeOnStart())
        QTimer::singleShot(400, this, &MainWindow::resumePrevious);
}

// Chats adopted before v1.1 carry claude's machine-derived names
// ("pavel-f4") — retitle them from their transcripts, once.
void MainWindow::fixDerivedTitlesOnce() {
    QSettings s(QStringLiteral("rezoom"), QStringLiteral("rezoom"));

    if (s.value("general/titles_fixed").toBool())
        return;

    static const QRegularExpression derivedRe("^[A-Za-z0-9_.]+-[0-9a-f]{2}$");
    store.mutate([](QList<Chat> &list) {
        for (Chat &c : list) {
            if (c.claudeSessionID.isEmpty() || !derivedRe.match(c.title).hasMatch())
                continue;

            const QString first = TranscriptIndex::previewForSession(c.claudeSessionID);

            if (!first.isEmpty())
                c.title = first.left(40);

            const QString last = TranscriptIndex::lastMessagePreview(c.claudeSessionID);

            if (!last.isEmpty())
                c.preview = last;
        }
    });
    s.setValue("general/titles_fixed", true);
}

// The running-embedded set is persisted on every launch/termination, not
// just on quit — so auto-resume works after a crash or a hard kill too.
void MainWindow::rememberRunning() {
    QStringList ids;

    for (auto it = panes.constBegin(); it != panes.constEnd(); ++it)
        ids << it.key();

    QSettings s(QStringLiteral("rezoom"), QStringLiteral("rezoom"));
    s.setValue("ui/runningChats", ids);
}

// Pre-accept claude's "trust this folder?" for the dirs we're about to
// auto-resume — the claude-restore trick. Only when NO claude is running
// (live sessions rewrite ~/.claude.json), never under test overrides.
void MainWindow::preTrustCwds(const QStringList &cwds) {
    if (cwds.isEmpty() || !qEnvironmentVariable("REZOOM_CLAUDE_DIR").isEmpty())
        return;

    if (!registry.bySessionID().isEmpty())
        return; // running claudes own that file right now

    const QString path = QDir::homePath() + "/.claude.json";
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return;

    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();

    if (root.isEmpty())
        return; // parse failure — do not touch the file

    QJsonObject projects = root["projects"].toObject();
    bool changed = false;

    for (const QString &cwd : cwds) {
        QJsonObject p = projects[cwd].toObject();

        if (p["hasTrustDialogAccepted"].toBool())
            continue;

        p["hasTrustDialogAccepted"] = true;
        projects[cwd] = p;
        changed = true;
    }

    if (!changed)
        return;

    root["projects"] = projects;
    QFile::remove(path + ".bak-rezoom");
    QFile::copy(path, path + ".bak-rezoom");
    QSaveFile out(path);

    if (!out.open(QIODevice::WriteOnly))
        return;

    out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    out.commit();
}

void MainWindow::resumePrevious() {
    QSettings s(QStringLiteral("rezoom"), QStringLiteral("rezoom"));
    const QStringList ids = s.value("ui/runningChats").toStringList();
    QStringList cwds;

    for (const QString &id : ids)
        if (const Chat *c = store.find(id); c && c->kind == "claude" && c->host.isEmpty())
            cwds << c->cwd;

    preTrustCwds(cwds);
    int slot = 0;

    for (const QString &id : ids) {
        const Chat *c = store.find(id);

        if (!c || panes.contains(id))
            continue;

        // Resumed externally since we quit? Leave it there — raise works.
        if (registry.entryForSession(c->claudeSessionID))
            continue;

        QTimer::singleShot(400 * slot++, this, [this, id] {
            if (!panes.contains(id))
                launchChat(id);
        });
    }
}

QWidget *MainWindow::buildLeftPanel() {
    auto *panel = new QWidget(this);
    panel->setMinimumWidth(240);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 0, 4);

    search = new QLineEdit(panel);

    // \xe2\x80\xa6 = UTF-8 for "…" (ellipsis)
    search->setPlaceholderText(tr("Search\xe2\x80\xa6  (Ctrl+Shift+F)"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, model, &ChatListModel::setFilter);
    layout->addWidget(search);

    list = new QListView(panel);
    list->setModel(model);
    list->setItemDelegate(new ChatDelegate(list));
    list->setUniformItemSizes(true);
    list->setMouseTracking(true);
    list->setFrameShape(QFrame::NoFrame);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(list->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onChatSelected);
    connect(list, &QListView::customContextMenuRequested, this, &MainWindow::showContextMenu);
    layout->addWidget(list, 1);

    auto *bottom = new QHBoxLayout;
    auto *newBtn = new QPushButton(tr("+ New"), panel);
    buildNewMenu(newBtn);
    bottom->addWidget(newBtn);

    archivedBtn = new QPushButton(panel);
    archivedBtn->setCheckable(true);
    archivedBtn->setFlat(true);
    connect(archivedBtn, &QPushButton::toggled, this, [this](bool on) {
        model->setShowArchived(on);
        updateArchivedButton();
    });
    bottom->addWidget(archivedBtn);
    bottom->addStretch(1);

    auto *settingsBtn = new QPushButton(tr("\xe2\x9a\x99"), panel); // \xe2\x9a\x99 = UTF-8 for "⚙" (gear)
    settingsBtn->setToolTip(tr("Settings \xe2\x80\x94 Ctrl+Shift+P")); // "—" (em dash)
    settingsBtn->setFlat(true);
    QFont gearFont = settingsBtn->font();
    gearFont.setPointSizeF(gearFont.pointSizeF() * 1.6);
    settingsBtn->setFont(gearFont);
    settingsBtn->setFixedWidth(40);
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);
    bottom->addWidget(settingsBtn);
    layout->addLayout(bottom);

    connect(&store, &SessionStore::changed, this, &MainWindow::updateArchivedButton);
    updateArchivedButton();

    return panel;
}

QWidget *MainWindow::buildRightPanel() {
    stack = new QStackedWidget(this);

    auto *label = new QLabel(
        tr("Select a session on the left \xe2\x80\x94 or hit + New (Ctrl+Shift+N)."), // \xe2\x80\x94 = "—" (em dash)
        this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: palette(placeholder-text); font-size: 15px;");
    emptyPage = label;
    stack->addWidget(emptyPage);

    return stack;
}

// Ctrl+Shift chords, Konsole-style — plain Ctrl+N etc. must keep reaching
// the shells inside embedded terminals.
void MainWindow::buildNewMenu(QPushButton *button) {
    auto *menu = new QMenu(button);

    const auto add = [this, menu](const QString &text, const char *chord, auto slot) {
        QAction *action = menu->addAction(text, this, slot);
        action->setShortcut(QKeySequence(QLatin1String(chord)));
        addAction(action); // window-wide, not only while the menu is open
    };

    add(tr("New Claude session\xe2\x80\xa6"), "Ctrl+Shift+N", &MainWindow::newClaude);
    add(tr("New terminal\xe2\x80\xa6"), "Ctrl+Shift+T", &MainWindow::newTerminal);
    add(tr("New SSH\xe2\x80\xa6"), "Ctrl+Shift+S", &MainWindow::newSsh);
    menu->addSeparator();
    add(tr("Adopt sessions\xe2\x80\xa6"), "Ctrl+Shift+A", &MainWindow::openAdopt);
    button->setMenu(menu);
}

void MainWindow::addChord(const char *chord, const char *what,
                          void (MainWindow::*slot)()) {
    auto *action = new QAction(tr(what), this);
    action->setShortcut(QKeySequence(QLatin1String(chord)));
    connect(action, &QAction::triggered, this, slot);
    addAction(action);
}

// Window-wide chords. Ctrl+Shift only (plus Ctrl+PgUp/PgDn) — plain Ctrl
// keys must keep reaching the shells inside embedded terminals.
void MainWindow::buildShortcuts() {
    addChord("Ctrl+Shift+Return", "Resume / beam / connect", &MainWindow::actOnCurrent);
    addChord("Ctrl+Shift+F", "Search chats", &MainWindow::focusSearch);
    addChord("Ctrl+PgDown", "Next chat", &MainWindow::nextChat);
    addChord("Ctrl+PgUp", "Previous chat", &MainWindow::prevChat);
    addChord("Ctrl+Shift+R", "Rename chat", &MainWindow::renameCurrent);
    addChord("Ctrl+Shift+E", "Archive chat", &MainWindow::archiveToggleCurrent);
    addChord("Ctrl+Shift+D", "Float chat", &MainWindow::floatToggleCurrent);
    addChord("Ctrl+Shift+O", "Pop out to Konsole", &MainWindow::popOutCurrent);
    addChord("Ctrl+Shift+W", "Close embedded pane", &MainWindow::closePaneCurrent);
    addChord("Ctrl+Shift+P", "Settings", &MainWindow::openSettings);
    addChord("Ctrl+Shift+/", "Keyboard shortcuts", &MainWindow::openShortcuts);
}

void MainWindow::actOnCurrent() {
    if (currentID.isEmpty())
        return;

    TerminalPane *pane = panes.value(currentID);

    if (pane) { // already running here — just focus it
        ChatView *view = views.value(currentID);
        FloatWindow *w = view ? floatOf(view) : 0;

        if (w)
            w->focusView(view);

        pane->setFocus();
        return;
    }

    const Chat *c = store.find(currentID);

    if (!c)
        return;

    if (registry.entryForSession(c->claudeSessionID))
        pullInLive(currentID); // running outside — beam it in
    else
        launchChat(currentID);
}

void MainWindow::renameCurrent() {
    if (!currentID.isEmpty())
        renameChat(currentID);
}

void MainWindow::archiveToggleCurrent() {
    const Chat *c = currentID.isEmpty() ? 0 : store.find(currentID);

    if (c)
        archiveChat(currentID, !c->archived);
}

void MainWindow::floatToggleCurrent() {
    if (currentID.isEmpty())
        return;

    ChatView *view = viewFor(currentID);
    FloatWindow *w = floatOf(view);

    if (w)
        returnToMain(w, view);
    else
        floatChat(currentID, 0);
}

void MainWindow::popOutCurrent() {
    if (!currentID.isEmpty())
        popOut(currentID);
}

void MainWindow::closePaneCurrent() {
    TerminalPane *pane = currentID.isEmpty() ? 0 : panes.value(currentID);

    if (!pane)
        return;

    if (pane->shellPID() > 0)
        kill(pane->shellPID(), SIGHUP);

    onPaneTerminated(currentID);
}

void MainWindow::stepChat(int delta) {
    const int count = model->rowCount();

    if (!count)
        return;

    int row = list->currentIndex().isValid() ? list->currentIndex().row() + delta : 0;
    row = qBound(0, row, count - 1);
    list->setCurrentIndex(model->index(row, 0));
}

void MainWindow::nextChat() {
    stepChat(1);
}

void MainWindow::prevChat() {
    stepChat(-1);
}

void MainWindow::focusSearch() {
    search->setFocus();
    search->selectAll();
}

void MainWindow::openShortcuts() {
    ShortcutsDialog dialog(this);
    dialog.exec();
}

void MainWindow::updateArchivedButton() {
    const int n = model->archivedCount();
    archivedBtn->setText(archivedBtn->isChecked() ? tr("\xe2\x86\x90 Back") // "←" (left arrow)
                                                  : tr("Archived (%1)").arg(n));
    archivedBtn->setVisible(archivedBtn->isChecked() || n > 0);
}

ChatView *MainWindow::viewFor(const QString &chatID) {
    ChatView *view = views.value(chatID);

    if (view)
        return view;

    view = new ChatView(chatID, this);
    views.insert(chatID, view);
    stack->addWidget(view);
    connect(view->resume(), &ResumePane::launchRequested, this,
            [this, chatID] { launchChat(chatID); });
    connect(view->resume(), &ResumePane::raiseRequested, this,
            [this](int pid) { raiseExternal(pid); });
    connect(view->resume(), &ResumePane::pullRequested, this,
            [this, chatID] { pullInLive(chatID); });
    refreshView(chatID);

    return view;
}

void MainWindow::refreshView(const QString &chatID) {
    ChatView *view = views.value(chatID);
    const Chat *c = store.find(chatID);

    if (!view || !c || view->terminal())
        return;

    int externalPID = 0;
    const auto live = registry.entryForSession(c->claudeSessionID);

    if (live)
        externalPID = live->pid;

    view->resume()->setChat(*c, templates.resolveFor(*c), externalPID);
}

void MainWindow::selectChat(const QString &chatID) {
    const QModelIndex idx = model->indexOf(chatID);

    if (idx.isValid()) {
        list->setCurrentIndex(idx);
        list->scrollTo(idx);
    }
}

void MainWindow::onChatSelected() {
    const QString id = model->idAt(list->currentIndex());

    if (id.isEmpty()) {
        currentID.clear();
        stack->setCurrentWidget(emptyPage);
        return;
    }

    currentID = id;

    if (unread.remove(id)) {
        model->setUnread(unread);
        updateAttention();
    }

    ChatView *view = viewFor(id);
    refreshView(id);
    FloatWindow *w = floatOf(view);

    if (w) {
        w->focusView(view);
        stack->setCurrentWidget(emptyPage);
    } else
        stack->setCurrentWidget(view);
}

void MainWindow::wirePane(TerminalPane *pane) {
    connect(pane, &TerminalPane::terminated, this, &MainWindow::onPaneTerminated);
    connect(pane, &TerminalPane::childClaude, this, &MainWindow::onChildClaude);
    connect(pane, &TerminalPane::childCodex, this, &MainWindow::onChildCodex);
    connect(pane, &TerminalPane::childSsh, this, &MainWindow::onChildSsh);
    connect(pane, &TerminalPane::childTmux, this, &MainWindow::onChildTmux);
    connect(pane, &TerminalPane::captionChanged, this,
            [this](const QString &id, const QString &caption) {
                if (liveTitles.value(id) == caption)
                    return;

                if (caption.isEmpty())
                    liveTitles.remove(id);
                else
                    liveTitles.insert(id, caption);

                model->setLiveTitles(liveTitles);
            });
}

void MainWindow::launchChat(const QString &chatID, const QString &commandOverride) {
    const Chat *c = store.find(chatID);

    if (!c || panes.contains(chatID))
        return;

    ChatView *view = viewFor(chatID);
    auto *pane = new TerminalPane(chatID, c->tint, view);

    if (!pane->valid()) {
        // No embeddable terminal (no konsolepart, or non-KDE platform like
        // macOS) — open the chat in an external terminal window instead.
        const QString why = pane->errorText();
        delete pane;

        const auto answer = QMessageBox::question(
            this, tr("Rezoom"),
            tr("No embedded terminal (%1).\n\nOpen this chat in an external terminal window?")
                .arg(why));

        if (answer == QMessageBox::Yes) {
            launchInKonsole((!c->cwd.isEmpty() && c->host.isEmpty()) ? c->cwd : QDir::homePath(),
                            templates.resolveFor(*c));
            store.touch(chatID);
        }

        return;
    }

    wirePane(pane);
    panes.insert(chatID, pane);
    rememberRunning();
    model->setEmbedded(QSet<QString>(panes.keyBegin(), panes.keyEnd()));
    view->attachPane(pane);

    const QString cwd = (!c->cwd.isEmpty() && c->host.isEmpty()) ? c->cwd : QDir::homePath();
    pane->runCommand(cwd, commandOverride.isEmpty() ? templates.resolveFor(*c) : commandOverride);
    store.touch(chatID);

    FloatWindow *w = floatOf(view);

    if (w)
        w->focusView(view);
    else
        stack->setCurrentWidget(view);

    selectChat(chatID); // the list must always show what the pane shows
}

void MainWindow::onPaneTerminated(const QString &chatID) {
    if (shuttingDown)
        return;

    TerminalPane *pane = panes.take(chatID);
    rememberRunning();
    model->setEmbedded(QSet<QString>(panes.keyBegin(), panes.keyEnd()));

    if (liveTitles.remove(chatID))
        model->setLiveTitles(liveTitles);
    ChatView *view = views.value(chatID);

    if (view)
        view->paneGone();

    if (pane)
        pane->deleteLater();

    refreshView(chatID);
}

void MainWindow::onChildClaude(const QString &chatID, int claudePID) {
    const auto live = registry.entryForPID(claudePID);

    if (!live)
        return;

    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    Chat c = *cp;
    bool changed = false;

    // /clear and forks change the session id under the same pane — track it.
    if (c.claudeSessionID != live->sessionID) {
        c.claudeSessionID = live->sessionID;
        changed = true;
    }

    if (c.kind == "shell") {
        c.kind = "claude";
        changed = true;
    }

    if (c.title.isEmpty()) {
        const QString first = TranscriptIndex::previewForSession(c.claudeSessionID);
        c.title = !first.isEmpty() ? first.left(40) : live->name;
        changed = !c.title.isEmpty();
    }

    if (c.preview.isEmpty()) {
        c.preview = TranscriptIndex::previewForSession(c.claudeSessionID);
        changed = changed || !c.preview.isEmpty();
    }

    if (changed)
        store.update(c);
}

// Codex has no live registry like ~/.claude/sessions — bind the process to
// the freshest rollout file matching the chat's cwd instead.
void MainWindow::onChildCodex(const QString &chatID, int codexPID) {
    Q_UNUSED(codexPID);
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    const TranscriptInfo t = CodexIndex::newestSession(cp->cwd);

    if (t.sessionID.isEmpty() || cp->claudeSessionID == t.sessionID)
        return;

    Chat c = *cp;
    c.kind = "codex";
    c.claudeSessionID = t.sessionID;

    if (c.title.isEmpty() && !t.preview.isEmpty())
        c.title = t.preview.left(40);

    if (!t.preview.isEmpty())
        c.preview = t.preview;

    store.update(c);
}

void MainWindow::onChildSsh(const QString &chatID, const QStringList &cmdline) {
    const Chat *cp = store.find(chatID);

    if (!cp || !cp->entryCommand.isEmpty())
        return;

    Chat c = *cp;
    c.entryCommand = cmdline.join(' ');
    c.host = ProcessScout::sshDestination(cmdline);

    if (c.kind == "shell")
        c.kind = "ssh";

    store.update(c);
}

void MainWindow::onChildTmux(const QString &chatID, const QStringList &cmdline) {
    const Chat *cp = store.find(chatID);

    if (!cp || !cp->tmuxSession.isEmpty())
        return;

    // "tmux attach -t name" / "tmux new -s name" — the -t/-s argument.
    Chat c = *cp;

    for (int i = 1; i + 1 < cmdline.size(); ++i)
        if (cmdline[i] == "-t" || cmdline[i] == "-s")
            c.tmuxSession = cmdline[i + 1];

    if (c.kind == "shell" && !c.tmuxSession.isEmpty())
        c.kind = "tmux";

    store.update(c);
}

// One chat's registry delta: follow claude's own session renames
// (titleLocked chats excepted), stream the transcript tail while busy, and
// persist the turn result + unread mark on busy→idle.
void MainWindow::scanChatDelta(const Chat &c, RegistryDeltas &d) {
    const auto live = registry.entryForSession(c.claudeSessionID);
    const QString now = live ? live->status : QString();
    const QString before = lastStatus.value(c.id);

    if (live && !c.titleLocked && live->nameSource != "derived"
        && !live->name.isEmpty() && c.title != live->name)
        d.titles.insert(c.id, live->name);

    if (now == "busy") {
        const QString tail = TranscriptIndex::lastMessagePreview(c.claudeSessionID);

        if (!tail.isEmpty())
            d.busyTails.insert(c.id, tail);
    }

    if (before == "busy" && now == "idle") {
        const QString last = TranscriptIndex::lastMessagePreview(c.claudeSessionID);

        if (!last.isEmpty() && last != c.preview)
            d.previews.insert(c.id, last);

        if (c.id != currentID) {
            unread.insert(c.id);
            d.unreadChanged = true;
        }
    }

    lastStatus.insert(c.id, now);
}

void MainWindow::onRegistryUpdated() {
    RegistryDeltas d = {};

    for (const Chat &c : store.chats())
        if (!c.claudeSessionID.isEmpty())
            scanChatDelta(c, d);

    const bool unreadChanged = d.unreadChanged;
    const QHash<QString, QString> freshPreviews = d.previews;
    const QHash<QString, QString> freshTitles = d.titles;
    const QHash<QString, QString> busyTails = d.busyTails;

    if (busyTails != livePreviews) {
        livePreviews = busyTails;
        model->setLivePreviews(livePreviews);
    }

    if (!freshPreviews.isEmpty() || !freshTitles.isEmpty())
        store.mutate([&freshPreviews, &freshTitles](QList<Chat> &list) {
            for (Chat &c : list) {
                if (freshPreviews.contains(c.id))
                    c.preview = freshPreviews.value(c.id);

                if (freshTitles.contains(c.id))
                    c.title = freshTitles.value(c.id);
            }
        });

    if (unreadChanged)
        model->setUnread(unread);

    // Resume panes flip between Rezoom/raise as external sessions come and go.
    for (auto it = views.constBegin(); it != views.constEnd(); ++it)
        refreshView(it.key());

    // A frozen session that went busy (or exited) is no longer frozen.
    const QStringList frozen = notifications.frozenSessions();

    for (const QString &sid : frozen) {
        const auto live = registry.entryForSession(sid);

        if (!live || live->status == "busy")
            notifications.clear(sid);
    }

    autoAdoptNew();
    updateAttention();
}

// "(2) Rezoom — 3 working · 5 waiting · 1 frozen": the parenthesized count
// is what needs the user (unread + frozen) and doubles as the taskbar badge
// (Unity LauncherEntry — Plasma renders it on the launcher icon).
void MainWindow::updateAttention() {
    int working = 0;
    int waiting = 0;

    for (const Chat &c : store.chats()) {
        const auto live = registry.entryForSession(c.claudeSessionID);

        if (!live)
            continue;

        if (live->status == "busy")
            ++working;
        else if (live->status == "idle")
            ++waiting;
    }

    const int frozen = notifications.frozenSessions().size();
    const int needsYou = unread.size() + frozen;
    QString title;

    if (needsYou)
        title += QStringLiteral("(%1) ").arg(needsYou);

    title += QStringLiteral("Rezoom");
    QStringList parts;

    if (working)
        parts << tr("%n working", 0, working);

    if (waiting)
        parts << tr("%n waiting", 0, waiting);

    if (frozen)
        parts << tr("%n frozen", 0, frozen);

    if (!parts.isEmpty())
        // \xe2\x80\x94 = UTF-8 for "—" (em dash), \xc2\xb7 = "·" (middle dot)
        title += QString::fromUtf8(" \xe2\x80\x94 ") + parts.join(QString::fromUtf8(" \xc2\xb7 "));

    setWindowTitle(title);

#ifdef REZOOM_HAVE_DBUS
    QDBusMessage badge = QDBusMessage::createSignal(
        QStringLiteral("/rezoom"), QStringLiteral("com.canonical.Unity.LauncherEntry"),
        QStringLiteral("Update"));
    badge << QStringLiteral("application://rezoom.desktop")
          << QVariantMap{{QStringLiteral("count"), qint64(needsYou)},
                         {QStringLiteral("count-visible"), needsYou > 0}};
    QDBusConnection::sessionBus().send(badge);
#endif
}

// The WhatsApp model: a new interactive claude session anywhere on the
// machine becomes a chat by itself. Sessions inside our own panes are
// excluded — onChildClaude() re-binds those (a /clear changes the sid, and
// adopting the new sid here would duplicate the chat).
void MainWindow::autoAdoptNew() {
    if (!templates.autoAdopt())
        return;

    QSet<int> embedded;

    for (TerminalPane *pane : panes)
        for (const auto &p : ProcessScout::findDescendants(pane->shellPID(), {"claude"}))
            embedded.insert(p.pid);

    QList<LiveEntry> fresh;

    for (const LiveEntry &e : registry.bySessionID())
        if (e.kind == "interactive" && !embedded.contains(e.pid)
            && !store.findByClaudeSession(e.sessionID))
            fresh.append(e);

    if (fresh.isEmpty())
        return;

    store.mutate([this, &fresh](QList<Chat> &list) {
        for (const LiveEntry &e : fresh) {
            const bool tracked = std::any_of(list.cbegin(), list.cend(),
                [&e](const Chat &c) { return c.claudeSessionID == e.sessionID; });

            if (!tracked)
                list.append(chatFromLive(e));
        }
    });
}

// Chat identity: title = the session's first user message (claude's own
// registry names like "pavel-f4" are machine-derived), preview = the most
// recent message (WhatsApp-style "last message" line).
Chat MainWindow::chatFromLive(const LiveEntry &e) {
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

    return c;
}

FloatWindow *MainWindow::floatOf(ChatView *view) const {
    for (FloatWindow *w : floats)
        if (w->views().contains(view))
            return w;

    return 0;
}

FloatWindow *MainWindow::makeFloatWindow(const QString &name) {
    auto *w = new FloatWindow(name, this);
    floats.append(w);
    connect(w, &FloatWindow::returnRequested, this, &MainWindow::returnToMain);
    connect(w, &FloatWindow::closing, this, &MainWindow::reclaimWindow);

    return w;
}

void MainWindow::dropFloat(FloatWindow *w) {
    floats.removeAll(w);
    w->deleteLater();
}

void MainWindow::floatChat(const QString &chatID, FloatWindow *target) {
    ChatView *view = viewFor(chatID);
    FloatWindow *from = floatOf(view);

    if (from && from == target) {
        target->focusView(view);
        return;
    }

    if (from)
        from->takeView(view);
    else
        stack->removeWidget(view);

    if (!target)
        target = makeFloatWindow(tr("Group %1").arg(++floatCounter));

    const Chat *c = store.find(chatID);
    target->addView(view, c && !c->title.isEmpty() ? c->title : chatID.left(8));
    target->focusView(view);

    if (from && !from->count())
        dropFloat(from);

    if (currentID == chatID)
        stack->setCurrentWidget(emptyPage);

    saveUiState();
}

void MainWindow::returnToMain(FloatWindow *w, ChatView *view) {
    w->takeView(view);
    stack->addWidget(view);

    if (!w->count())
        dropFloat(w);

    selectChat(view->chatID());

    if (currentID == view->chatID())
        stack->setCurrentWidget(view);

    saveUiState();
}

void MainWindow::reclaimWindow(FloatWindow *w) {
    if (shuttingDown)
        return;

    const QList<ChatView *> vs = w->views();

    for (ChatView *view : vs) {
        w->takeView(view);
        stack->addWidget(view);
    }

    dropFloat(w);
    saveUiState();
}

void MainWindow::buildFloatMenu(QMenu *menu, const QString &chatID) {
    ChatView *view = views.value(chatID);
    FloatWindow *from = view ? floatOf(view) : 0;
    QMenu *moveMenu = menu->addMenu(tr("Show in window"));

    QAction *mainAct = moveMenu->addAction(tr("Main window"));
    mainAct->setCheckable(true);
    mainAct->setChecked(!from);
    connect(mainAct, &QAction::triggered, this, [this, from, view] {
        if (from && view)
            returnToMain(from, view);
    });

    for (FloatWindow *w : floats) {
        QAction *act = moveMenu->addAction(w->name());
        act->setCheckable(true);
        act->setChecked(from == w);
        connect(act, &QAction::triggered, this, [this, chatID, w] { floatChat(chatID, w); });
    }

    moveMenu->addSeparator();
    moveMenu->addAction(tr("New floating window"), this,
                        [this, chatID] { floatChat(chatID, 0); });
}

void MainWindow::showContextMenu(const QPoint &pos) {
    const QModelIndex idx = list->indexAt(pos);

    if (!idx.isValid())
        return;

    const QString id = model->idAt(idx);
    const Chat *c = store.find(id);

    if (!c)
        return;

    const auto chord = [](QAction *action, const char *keys) {
        action->setShortcut(QKeySequence(QLatin1String(keys)));
        action->setShortcutContext(Qt::WidgetShortcut); // display only — window action fires
    };
    QMenu menu(this);
    chord(menu.addAction(tr("Rename\xe2\x80\xa6"), this, [this, id] { renameChat(id); }),
          "Ctrl+Shift+R");
    menu.addAction(tr("Edit resume command\xe2\x80\xa6"), this, [this, id] { editCommand(id); });
    menu.addSeparator();
    buildFloatMenu(&menu, id);
    chord(menu.addAction(tr("Pop out to Konsole"), this, [this, id] { popOut(id); }),
          "Ctrl+Shift+O");

    const auto live = registry.entryForSession(c->claudeSessionID);

    if (live && !panes.contains(id)) {
        const int pid = live->pid;
        menu.addAction(tr("Go to its window"), this, [this, pid] { raiseExternal(pid); });

        // \xe2\xa4\xb5 = UTF-8 for "⤵"
        menu.addAction(tr("\xe2\xa4\xb5 Beam it in"), this, [this, id] { pullInLive(id); });
    }

    if (c->kind == "ssh")
        menu.addAction(tr("Scan remote for claude/tmux\xe2\x80\xa6"), this,
                       [this, id] { scanRemote(id); });

    menu.addSeparator();
    chord(menu.addAction(c->archived ? tr("Unarchive") : tr("Archive"), this,
                         [this, id, on = !c->archived] { archiveChat(id, on); }),
          "Ctrl+Shift+E");
    menu.addAction(tr("Delete\xe2\x80\xa6"), this, [this, id] { deleteChat(id); });
    menu.exec(list->viewport()->mapToGlobal(pos));
}

void MainWindow::renameChat(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    bool ok = false;
    const QString title = getTextSelectable(this, tr("Rename"), tr("Title:"), cp->title, &ok);

    if (!ok)
        return;

    Chat c = *cp;
    c.title = title;
    c.titleLocked = !title.trimmed().isEmpty(); // empty rename = back to auto
    store.update(c);

    ChatView *view = views.value(chatID);
    FloatWindow *w = view ? floatOf(view) : 0;

    if (w)
        w->setTabTitle(view, title);

    refreshView(chatID);
}

void MainWindow::editCommand(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    bool ok = false;
    const QString hint = tr("Resume command (empty = template \"%1\": %2)")
                             .arg(templates.defaultTemplateFor(*cp), templates.resolveFor(*cp));
    const QString cmd = getTextSelectable(this, tr("Edit resume command"), hint,
                                          cp->commandOverride, &ok);

    if (!ok)
        return;

    Chat c = *cp;
    c.commandOverride = cmd;
    store.update(c);
    refreshView(chatID);
}

void MainWindow::archiveChat(const QString &chatID, bool on) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    Chat c = *cp;
    c.archived = on;
    store.update(c);
}

void MainWindow::deleteChat(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    const auto answer = QMessageBox::question(
        this, tr("Delete chat"),
        tr("Remove \"%1\" from Rezoom?\nThe claude transcript itself is not touched.")
            .arg(cp->title));

    if (answer != QMessageBox::Yes)
        return;

    ChatView *view = views.take(chatID);

    if (view) {
        FloatWindow *w = floatOf(view);

        if (w) {
            w->takeView(view);

            if (!w->count())
                dropFloat(w);
        }

        view->deleteLater();
    }

    panes.remove(chatID);
    store.remove(chatID);
}

// Move a session running outside Rezoom into an embedded pane without
// killing it: run reptyr against its pid in a fresh pane. A refused ptrace
// attach leaves the original session untouched, so on failure we can offer
// kill-and-resume — for claude chats that path is lossless anyway.
void MainWindow::pullInLive(const QString &chatID) {
    const Chat *c = store.find(chatID);

    if (!c || panes.contains(chatID))
        return;

    const auto live = registry.entryForSession(c->claudeSessionID);

    if (!live)
        return;

    if (QStandardPaths::findExecutable(QStringLiteral("reptyr")).isEmpty()) {
        offerPullRecovery(chatID, live->pid,
                          tr("Live pull needs reptyr, which is not installed."),
                          QStringLiteral("apt-get install -y reptyr"));
        return;
    }

    const int pid = live->pid;
    launchChat(chatID, QStringLiteral("reptyr %1").arg(pid));
    QTimer::singleShot(2500, this, [this, chatID, pid] { verifyPull(chatID, pid); });
}

void MainWindow::verifyPull(const QString &chatID, int pid) {
    TerminalPane *pane = panes.value(chatID);

    if (!pane)
        return; // pane gone meanwhile — nothing to verify

    // On success reptyr stays alive under our shell as the tty forwarder.
    if (!ProcessScout::findDescendants(pane->shellPID(), {"reptyr"}).isEmpty())
        return;

    offerPullRecovery(chatID, pid,
                      tr("Live pull failed (reptyr's error is shown in the terminal). "
                         "Usual cause: ptrace is restricted."),
                      QStringLiteral("setcap cap_sys_ptrace+ep %1")
                          .arg(QStandardPaths::findExecutable(QStringLiteral("reptyr"))));
}

void MainWindow::closeAttemptPane(const QString &chatID) {
    TerminalPane *pane = panes.value(chatID);

    if (!pane)
        return;

    if (pane->shellPID() > 0)
        kill(pane->shellPID(), SIGHUP);

    onPaneTerminated(chatID);
}

enum class Recovery { Fix, Retry, Fallback, Cancel };

// Shared recovery dialog for a failed live move (either direction): fix the
// environment right here (pkexec pops the password dialog), copy the command,
// retry, or take the destructive fallback. No copy-and-lose-the-dialog trips.
static Recovery recoveryDialog(QWidget *parent, const QString &why,
                               const QString &fixCommand, const QString &fallbackLabel) {
    QMessageBox box(QMessageBox::Question, QObject::tr("Live move"),
                    fixCommand.isEmpty()
                        ? why
                        : QObject::tr("%1\n\nFix runs as admin (you'll be asked for "
                                      "your password):\npkexec %2").arg(why, fixCommand),
                    QMessageBox::NoButton, parent);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *fixBtn = fixCommand.isEmpty()
        ? 0
        : box.addButton(QObject::tr("Fix && retry"), QMessageBox::AcceptRole);
    QPushButton *copyBtn = fixCommand.isEmpty()
        ? 0
        : box.addButton(QObject::tr("Copy command"), QMessageBox::ActionRole);
    QPushButton *retryBtn = box.addButton(QObject::tr("Retry"), QMessageBox::ActionRole);
    QPushButton *fallbackBtn = box.addButton(fallbackLabel, QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);

    if (copyBtn) {
        // Copy must not close the dialog: detach the button from the box.
        copyBtn->disconnect();
        QObject::connect(copyBtn, &QPushButton::clicked, copyBtn, [copyBtn, fixCommand] {
            QGuiApplication::clipboard()->setText(fixCommand);
            copyBtn->setText(QObject::tr("Copied")); // \xe2\x9c\x93 would be nicer but plain is clear
        });
    }

    box.exec();

    if (fixBtn && box.clickedButton() == fixBtn) {
        QProcess fix;
        fix.start(QStringLiteral("pkexec"), QProcess::splitCommand(fixCommand));
        fix.waitForFinished(120000); // the password dialog takes human time

        return Recovery::Fix;
    }

    if (box.clickedButton() == retryBtn)
        return Recovery::Retry;

    if (box.clickedButton() == fallbackBtn)
        return Recovery::Fallback;

    return Recovery::Cancel;
}

void MainWindow::offerPullRecovery(const QString &chatID, int pid, const QString &why,
                                   const QString &fixCommand) {
    const Recovery r = recoveryDialog(this, why, fixCommand, tr("Kill && resume here"));

    if (r == Recovery::Fix || r == Recovery::Retry) {
        closeAttemptPane(chatID);
        pullInLive(chatID);
    } else if (r == Recovery::Fallback) {
        closeAttemptPane(chatID);
        kill(pid, SIGTERM);
        resumeWhenGone(chatID, pid, 20);
    }
}

// Resume the chat embedded once the external process is really gone
// (polling ~5 s in 250 ms ticks before giving up).
void MainWindow::resumeWhenGone(const QString &chatID, int pid, int triesLeft) {
    if (LiveRegistry::pidAlive(pid)) {
        if (!triesLeft) {
            QMessageBox::warning(this, tr("Pull into Rezoom"),
                                 tr("The external session (pid %1) did not exit.").arg(pid));
            return;
        }

        QTimer::singleShot(250, this, [this, chatID, pid, triesLeft] {
            resumeWhenGone(chatID, pid, triesLeft - 1);
        });
        return;
    }

    registry.rescan(); // drop the stale entry before relaunching
    launchChat(chatID);
    selectChat(chatID);
}

// The interesting process inside a pane (claude/codex/ssh), else the shell.
int MainWindow::paneTargetPid(TerminalPane *pane) const {
    const auto procs =
        ProcessScout::findDescendants(pane->shellPID(), {"claude", "codex", "ssh"});

    return procs.isEmpty() ? pane->shellPID() : procs.first().pid;
}

// Pop out mirrors beam-in: try the live move (reptyr running in the new
// Konsole window steals the process — nothing killed), verify, and only
// offer kill-and-reopen as the fallback.
void MainWindow::popOut(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    TerminalPane *pane = panes.value(chatID);

    if (!pane) { // nothing running here — plain external launch
        launchInKonsole(cp->host.isEmpty() ? cp->cwd : QString(), templates.resolveFor(*cp));
        return;
    }

    const int target = paneTargetPid(pane);

    if (QStandardPaths::findExecutable(QStringLiteral("reptyr")).isEmpty()) {
        popOutRecovery(chatID, tr("Live move needs reptyr, which is not installed."),
                       QStringLiteral("apt-get install -y reptyr"));
        return;
    }

    const QString before = ProcessScout::tty(target);
    launchInKonsole(cp->host.isEmpty() ? cp->cwd : QString(),
                    QStringLiteral("reptyr %1").arg(target));
    QTimer::singleShot(2500, this, [this, chatID, target, before] {
        verifyPopOut(chatID, target, before);
    });
}

void MainWindow::verifyPopOut(const QString &chatID, int target, const QString &beforeTty) {
    const QString now = ProcessScout::tty(target);

    if (!now.isEmpty() && now != beforeTty) {
        closeAttemptPane(chatID); // moved out — retire the emptied pane
        return;
    }

    popOutRecovery(chatID,
                   tr("Live move failed (reptyr's error is in the new Konsole window "
                      "\xe2\x80\x94 close it). Usual cause: ptrace is restricted."),
                   // \xe2\x80\x94 = UTF-8 for em dash
                   QStringLiteral("setcap cap_sys_ptrace+ep %1")
                       .arg(QStandardPaths::findExecutable(QStringLiteral("reptyr"))));
}

void MainWindow::popOutRecovery(const QString &chatID, const QString &why,
                                const QString &fixCommand) {
    const Recovery r = recoveryDialog(this, why, fixCommand, tr("Kill && reopen in Konsole"));

    if (r == Recovery::Fix || r == Recovery::Retry) {
        popOut(chatID);
        return;
    }

    if (r == Recovery::Fallback) {
        const Chat *cp = store.find(chatID);
        TerminalPane *pane = panes.value(chatID);

        if (pane) {
            if (pane->shellPID() > 0)
                kill(pane->shellPID(), SIGHUP);

            onPaneTerminated(chatID);
        }

        if (cp)
            launchInKonsole(cp->host.isEmpty() ? cp->cwd : QString(),
                            templates.resolveFor(*cp));
    }
}

void MainWindow::raiseExternal(int pid) {
    const QString script = QDir::homePath() + "/.claude/raise-konsole.sh";

    if (!QFile::exists(script))
        return;

    QProcess proc;
    proc.setProgram("bash");
    proc.setArguments({script});
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("RAISE_KONSOLE_START_PID", QString::number(pid));
    proc.setProcessEnvironment(env);
    proc.startDetached();
}

struct RemoteFinding {
    QString kind; // "claude" | "tmux"
    QString sessionID;
    QString cwd;
    QString name;
};

static QList<RemoteFinding> parseRemoteScan(const QString &output) {
    QList<RemoteFinding> out;
    const QStringList halves = output.split("===TMUX===");
    const QStringList jsonLines = halves.value(0).split('\n', Qt::SkipEmptyParts);

    for (const QString &line : jsonLines) {
        const QJsonObject o = QJsonDocument::fromJson(line.toUtf8()).object();

        if (o["sessionId"].toString().isEmpty())
            continue;

        RemoteFinding f = {};
        f.kind = "claude";
        f.sessionID = o["sessionId"].toString();
        f.cwd = o["cwd"].toString();
        f.name = o["name"].toString();
        out.append(f);
    }

    const QStringList tmuxLines = halves.value(1).split('\n', Qt::SkipEmptyParts);

    for (const QString &name : tmuxLines) {
        RemoteFinding f = {};
        f.kind = "tmux";
        f.name = name.trimmed();
        out.append(f);
    }

    return out;
}

// Build a concrete resume command by splicing -t into the recorded entry
// command ("ssh host ..." → "ssh -t host ... '<remote command>'").
static QString remoteResumeOverride(const QString &entry, const RemoteFinding &f) {
    QString remote;

    if (f.kind == "claude")
        remote = QStringLiteral("'cd %1 && claude --resume %2'").arg(f.cwd, f.sessionID);
    else
        remote = QStringLiteral("tmux attach -t %1").arg(f.name);

    if (entry.startsWith("ssh "))
        return "ssh -t " + entry.mid(4) + " " + remote;

    return entry + " " + remote; // alias entry — may need a manual tweak for tty
}

void MainWindow::scanRemote(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp || cp->entryCommand.isEmpty())
        return;

    const QString probe = cp->entryCommand
        + " 'for f in ~/.claude/sessions/*.json; do cat \"$f\" 2>/dev/null; echo; done; "
          "echo ===TMUX===; tmux ls -F \"#{session_name}\" 2>/dev/null; true'";
    if (!askSelectable(this, tr("Scan remote"),
                       tr("Run this to look for claude sessions and tmux on the remote "
                          "host?\n\n%1").arg(probe)))
        return;

    QProcess proc;
    proc.start("zsh", {"-ic", probe}); // interactive zsh so user aliases work

    if (!proc.waitForFinished(20000) || proc.exitStatus() != QProcess::NormalExit) {
        proc.kill();
        QMessageBox::warning(this, tr("Scan remote"),
                             tr("Scan failed or timed out:\n%1")
                                 .arg(QString::fromUtf8(proc.readAllStandardError()).right(500)));
        return;
    }

    QList<RemoteFinding> found = parseRemoteScan(QString::fromUtf8(proc.readAllStandardOutput()));
    found.removeIf([this](const RemoteFinding &f) {
        return !f.sessionID.isEmpty() && store.findByClaudeSession(f.sessionID);
    });

    if (found.isEmpty()) {
        QMessageBox::information(this, tr("Scan remote"),
                                 tr("Nothing new found on the remote host."));
        return;
    }

    adoptRemoteFindings(*cp, found);
}

void MainWindow::adoptRemoteFindings(const Chat &base, const QList<RemoteFinding> &found) {
    QStringList lines;

    for (const RemoteFinding &f : found)
        lines << (f.kind == "claude" ? f.name + "  (" + f.cwd + ")" : "tmux: " + f.name);

    if (!askSelectable(this, tr("Scan remote"),
                       tr("Found on %1:\n\n%2\n\nAdopt all as chats?")
                           .arg(base.host, lines.join('\n'))))
        return;

    for (const RemoteFinding &f : found) {
        Chat c = Chat::create("ssh");
        c.host = base.host;
        c.entryCommand = base.entryCommand;
        c.claudeSessionID = f.sessionID;
        c.cwd = f.cwd;
        c.tmuxSession = f.kind == "tmux" ? f.name : QString();
        c.title = (f.kind == "claude" ? f.name : "tmux: " + f.name) + "@" + base.host;
        c.commandOverride = remoteResumeOverride(base.entryCommand, f);
        store.add(c);
    }
}

void MainWindow::resumeByQuery(const QString &query) {
    for (const Chat &c : store.chats()) {
        if (!c.id.startsWith(query) && !c.claudeSessionID.startsWith(query)
            && !c.title.contains(query, Qt::CaseInsensitive))
            continue;

        selectChat(c.id);

        if (!registry.entryForSession(c.claudeSessionID))
            launchChat(c.id);

        return;
    }
}

void MainWindow::newClaude() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Project directory"),
                                                          QDir::homePath());

    if (dir.isEmpty())
        return;

    Chat c = Chat::create("claude");
    c.cwd = dir;
    c.title = QDir(dir).dirName();
    store.add(c);
    selectChat(c.id);
    launchChat(c.id);
}

void MainWindow::newTerminal() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Start directory"),
                                                          QDir::homePath());

    if (dir.isEmpty())
        return;

    Chat c = Chat::create("shell");
    c.cwd = dir;
    c.title = QDir(dir).dirName();
    store.add(c);
    selectChat(c.id);
    launchChat(c.id);
}

void MainWindow::newSsh() {
    bool ok = false;
    const QString entry = getTextSelectable(
        this, tr("New SSH session"),
        tr("Connect command (e.g. \"ssh dev.example.com\" or an alias):"),
        QStringLiteral("ssh "), &ok);

    if (!ok || entry.trimmed().isEmpty())
        return;

    Chat c = Chat::create("ssh");
    c.entryCommand = entry.trimmed();
    c.host = ProcessScout::sshDestination(entry.trimmed().split(' ', Qt::SkipEmptyParts));
    c.title = c.host.isEmpty() ? c.entryCommand : c.host;
    store.add(c);
    selectChat(c.id); // not launched — connecting is the user's click
}

void MainWindow::openAdopt() {
    AdoptDialog dialog(&store, &registry, this);
    dialog.exec();
}

void MainWindow::openSettings() {
    SettingsDialog dialog(&templates, this);

    if (dialog.exec() == QDialog::Accepted && !currentID.isEmpty())
        refreshView(currentID);
}

void MainWindow::saveUiState() {
    QSettings s(QStringLiteral("rezoom"), QStringLiteral("rezoom"));
    s.setValue("ui/mainGeometry", saveGeometry());
    s.setValue("ui/splitter", splitter->saveState());

    s.beginWriteArray("ui/floats");
    int i = 0;

    for (FloatWindow *w : floats) {
        s.setArrayIndex(i++);
        s.setValue("name", w->name());
        s.setValue("geometry", w->saveGeometry());
        QStringList ids;

        for (ChatView *view : w->views())
            ids << view->chatID();

        s.setValue("chats", ids);
    }

    s.endArray();
}

void MainWindow::restoreUiState() {
    QSettings s(QStringLiteral("rezoom"), QStringLiteral("rezoom"));
    restoreGeometry(s.value("ui/mainGeometry").toByteArray());
    splitter->restoreState(s.value("ui/splitter").toByteArray());

    const int n = s.beginReadArray("ui/floats");
    floatCounter = n;

    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        QStringList ids = s.value("chats").toStringList();
        ids.removeIf([this](const QString &id) { return !store.find(id); });

        if (ids.isEmpty())
            continue;

        FloatWindow *w = makeFloatWindow(s.value("name").toString());
        w->restoreGeometry(s.value("geometry").toByteArray());

        for (const QString &id : ids) {
            ChatView *view = viewFor(id);
            stack->removeWidget(view);
            const Chat *c = store.find(id);
            w->addView(view, c && !c->title.isEmpty() ? c->title : id.left(8));
        }

        w->show();
    }

    s.endArray();
}

int MainWindow::livePaneCount() const {
    int n = 0;

    for (TerminalPane *pane : panes)
        if (pane->shellAlive())
            ++n;

    return n;
}

void MainWindow::closeEvent(QCloseEvent *ev) {
    const int live = livePaneCount();

    if (live > 0 && templates.confirmClose()) {
        const auto answer = QMessageBox::question(
            this, tr("Quit Rezoom"),
            tr("%n embedded session(s) still running. They stay resumable \xe2\x80\x94 quit?",
               // \xe2\x80\x94 = UTF-8 for "—" (em dash)
               nullptr, live));

        if (answer != QMessageBox::Yes) {
            ev->ignore();
            return;
        }
    }

    saveUiState();
    shuttingDown = true;
    const QList<FloatWindow *> ws = floats;

    for (FloatWindow *w : ws)
        w->close();

    ev->accept();
}
