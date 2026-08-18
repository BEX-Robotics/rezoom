#include <signal.h>

#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
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
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

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
#include "terminalpane.h"

// The user's tinted-profile wrapper if present, plain konsole otherwise.
static QString konsoleBinary() {
    const QString wrapper = QDir::homePath() + "/.local/bin/konsole";

    if (QFile::exists(wrapper))
        return wrapper;

    return QStringLiteral("konsole");
}

static void launchInKonsole(const QString &cwd, const QString &command) {
    QStringList args;

    if (!cwd.isEmpty())
        args << "--workdir" << cwd;

    if (!command.trimmed().isEmpty())
        args << "-e" << "zsh" << "-ic" << command;

    QProcess::startDetached(konsoleBinary(), args);
}

MainWindow::MainWindow() {
    setWindowTitle(QStringLiteral("Rezoom"));
    resize(1200, 760);

    model = new ChatListModel(&store, &registry, this);

    splitter = new QSplitter(this);
    splitter->addWidget(buildLeftPanel());
    splitter->addWidget(buildRightPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 900});
    setCentralWidget(splitter);

    connect(&registry, &LiveRegistry::updated, this, &MainWindow::onRegistryUpdated);
    onRegistryUpdated();
    restoreUiState();

    if (store.chats().isEmpty())
        QMetaObject::invokeMethod(this, &MainWindow::openAdopt, Qt::QueuedConnection);
}

QWidget *MainWindow::buildLeftPanel() {
    auto *panel = new QWidget(this);
    panel->setMinimumWidth(240);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 0, 4);

    search = new QLineEdit(panel);
    search->setPlaceholderText(tr("Search\xe2\x80\xa6")); // \xe2\x80\xa6 = UTF-8 for "…" (ellipsis)
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
    settingsBtn->setFlat(true);
    settingsBtn->setFixedWidth(32);
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
        tr("Select a session on the left \xe2\x80\x94 or hit + New."), // \xe2\x80\x94 = "—" (em dash)
        this);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: palette(placeholder-text); font-size: 15px;");
    emptyPage = label;
    stack->addWidget(emptyPage);

    return stack;
}

void MainWindow::buildNewMenu(QPushButton *button) {
    auto *menu = new QMenu(button);
    menu->addAction(tr("New Claude session\xe2\x80\xa6"), this, &MainWindow::newClaude);
    menu->addAction(tr("New terminal\xe2\x80\xa6"), this, &MainWindow::newTerminal);
    menu->addAction(tr("New SSH\xe2\x80\xa6"), this, &MainWindow::newSsh);
    menu->addSeparator();
    menu->addAction(tr("Adopt sessions\xe2\x80\xa6"), this, &MainWindow::openAdopt);
    button->setMenu(menu);
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

    if (idx.isValid())
        list->setCurrentIndex(idx);
}

void MainWindow::onChatSelected() {
    const QString id = model->idAt(list->currentIndex());

    if (id.isEmpty()) {
        currentID.clear();
        stack->setCurrentWidget(emptyPage);
        return;
    }

    currentID = id;

    if (unread.remove(id))
        model->setUnread(unread);

    ChatView *view = viewFor(id);
    refreshView(id);
    FloatWindow *w = floatOf(view);

    if (w) {
        w->focusView(view);
        stack->setCurrentWidget(emptyPage);
    } else
        stack->setCurrentWidget(view);
}

void MainWindow::launchChat(const QString &chatID) {
    const Chat *c = store.find(chatID);

    if (!c || panes.contains(chatID))
        return;

    ChatView *view = viewFor(chatID);
    auto *pane = new TerminalPane(chatID, c->tint, view);

    if (!pane->valid()) {
        QMessageBox::warning(this, tr("Rezoom"),
                             tr("Could not load the Konsole component:\n%1").arg(pane->errorText()));
        delete pane;
        return;
    }

    connect(pane, &TerminalPane::terminated, this, &MainWindow::onPaneTerminated);
    connect(pane, &TerminalPane::childClaude, this, &MainWindow::onChildClaude);
    connect(pane, &TerminalPane::childSsh, this, &MainWindow::onChildSsh);
    connect(pane, &TerminalPane::childTmux, this, &MainWindow::onChildTmux);

    panes.insert(chatID, pane);
    view->attachPane(pane);

    const QString cwd = (!c->cwd.isEmpty() && c->host.isEmpty()) ? c->cwd : QDir::homePath();
    pane->runCommand(cwd, templates.resolveFor(*c));
    store.touch(chatID);

    FloatWindow *w = floatOf(view);

    if (w)
        w->focusView(view);
    else
        stack->setCurrentWidget(view);
}

void MainWindow::onPaneTerminated(const QString &chatID) {
    if (shuttingDown)
        return;

    TerminalPane *pane = panes.take(chatID);
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

    if (c.title.isEmpty() && !live->name.isEmpty()) {
        c.title = live->name;
        changed = true;
    }

    if (c.preview.isEmpty()) {
        c.preview = TranscriptIndex::previewForSession(c.claudeSessionID);
        changed = changed || !c.preview.isEmpty();
    }

    if (changed)
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

void MainWindow::onRegistryUpdated() {
    bool unreadChanged = false;

    for (const Chat &c : store.chats()) {
        if (c.claudeSessionID.isEmpty())
            continue;

        const auto live = registry.entryForSession(c.claudeSessionID);
        const QString now = live ? live->status : QString();
        const QString before = lastStatus.value(c.id);

        // busy → idle while not focused = claude finished something for you.
        if (before == "busy" && now == "idle" && c.id != currentID) {
            unread.insert(c.id);
            unreadChanged = true;
        }

        lastStatus.insert(c.id, now);
    }

    if (unreadChanged)
        model->setUnread(unread);

    // Resume panes flip between Rezoom/raise as external sessions come and go.
    for (auto it = views.constBegin(); it != views.constEnd(); ++it)
        refreshView(it.key());
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

    QMenu menu(this);
    menu.addAction(tr("Rename\xe2\x80\xa6"), this, [this, id] { renameChat(id); });
    menu.addAction(tr("Edit resume command\xe2\x80\xa6"), this, [this, id] { editCommand(id); });
    menu.addSeparator();
    buildFloatMenu(&menu, id);
    menu.addAction(tr("Pop out to Konsole"), this, [this, id] { popOut(id); });

    const auto live = registry.entryForSession(c->claudeSessionID);

    if (live && !panes.contains(id)) {
        const int pid = live->pid;
        menu.addAction(tr("Raise external window"), this, [this, pid] { raiseExternal(pid); });
    }

    if (c->kind == "ssh")
        menu.addAction(tr("Scan remote for claude/tmux\xe2\x80\xa6"), this,
                       [this, id] { scanRemote(id); });

    menu.addSeparator();
    menu.addAction(c->archived ? tr("Unarchive") : tr("Archive"), this,
                   [this, id, on = !c->archived] { archiveChat(id, on); });
    menu.addAction(tr("Delete\xe2\x80\xa6"), this, [this, id] { deleteChat(id); });
    menu.exec(list->viewport()->mapToGlobal(pos));
}

void MainWindow::renameChat(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    bool ok = false;
    const QString title = QInputDialog::getText(this, tr("Rename"), tr("Title:"),
                                                QLineEdit::Normal, cp->title, &ok);

    if (!ok)
        return;

    Chat c = *cp;
    c.title = title;
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
    const QString cmd = QInputDialog::getText(this, tr("Edit resume command"), hint,
                                              QLineEdit::Normal, cp->commandOverride, &ok);

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

void MainWindow::popOut(const QString &chatID) {
    const Chat *cp = store.find(chatID);

    if (!cp)
        return;

    TerminalPane *pane = panes.value(chatID);

    if (pane) {
        const auto answer = QMessageBox::question(
            this, tr("Pop out"),
            tr("Close the embedded terminal and reopen this session in a Konsole window?"));

        if (answer != QMessageBox::Yes)
            return;

        if (pane->shellPID() > 0)
            kill(pane->shellPID(), SIGHUP);

        onPaneTerminated(chatID);
    }

    launchInKonsole(cp->host.isEmpty() ? cp->cwd : QString(), templates.resolveFor(*cp));
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
    const auto consent = QMessageBox::question(
        this, tr("Scan remote"),
        tr("Run this to look for claude sessions and tmux on the remote host?\n\n%1").arg(probe));

    if (consent != QMessageBox::Yes)
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

    const auto answer = QMessageBox::question(
        this, tr("Scan remote"),
        tr("Found on %1:\n\n%2\n\nAdopt all as chats?").arg(base.host, lines.join('\n')));

    if (answer != QMessageBox::Yes)
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
    const QString entry = QInputDialog::getText(
        this, tr("New SSH session"),
        tr("Connect command (e.g. \"ssh dev.example.com\" or an alias):"),
        QLineEdit::Normal, QStringLiteral("ssh "), &ok);

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
