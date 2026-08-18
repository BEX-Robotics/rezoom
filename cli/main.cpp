#include <stdio.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>

#include "core/liveregistry.h"
#include "core/sessionstore.h"
#include "core/templates.h"
#include "core/transcriptindex.h"

// rezoom-cli — scriptable access to the same store the GUI uses.
//   list                     chats + live presence (TSV)
//   resume <query> [--print] resume a chat in a Konsole window (or print the command)
//   adopt-running            adopt every untracked running claude session

static QString konsoleBinary() {
    const QString wrapper = QDir::homePath() + "/.local/bin/konsole";

    if (QFile::exists(wrapper))
        return wrapper;

    return QStringLiteral("konsole");
}

static int cmdList(SessionStore &store, LiveRegistry &registry) {
    for (const Chat &c : store.chats()) {
        const auto live = registry.entryForSession(c.claudeSessionID);
        const QString presence = live ? live->status : QStringLiteral("-");
        printf("%s\t%s\t%s\t%s\t%s\t%s\n", qPrintable(c.id.left(8)), qPrintable(presence),
               qPrintable(c.kind), qPrintable(c.title), qPrintable(c.cwd),
               qPrintable(c.claudeSessionID.left(8)));
    }

    return 0;
}

static int cmdResume(SessionStore &store, LiveRegistry &registry, Templates &templates,
                     const QString &query, bool printOnly) {

    QList<const Chat *> hits;

    for (const Chat &c : store.chats())
        if (c.id.startsWith(query) || c.claudeSessionID.startsWith(query)
            || c.title.contains(query, Qt::CaseInsensitive))
            hits.append(&c);

    if (hits.isEmpty()) {
        fprintf(stderr, "no chat matches '%s'\n", qPrintable(query));
        return 1;
    }

    if (hits.size() > 1) {
        fprintf(stderr, "ambiguous — %d matches:\n", (int)hits.size());

        for (const Chat *c : hits)
            fprintf(stderr, "  %s  %s\n", qPrintable(c->id.left(8)), qPrintable(c->title));

        return 2;
    }

    const Chat *c = hits[0];
    const auto live = registry.entryForSession(c->claudeSessionID);

    if (live) {
        fprintf(stderr, "already running (pid %d, %s)\n", live->pid, qPrintable(live->status));
        return 3;
    }

    const QString command = templates.resolveFor(*c);

    if (printOnly) {
        printf("%s\n", qPrintable(command));
        return 0;
    }

    QStringList args;

    if (!c->cwd.isEmpty() && c->host.isEmpty())
        args << "--workdir" << c->cwd;

    if (!command.isEmpty())
        args << "-e" << "zsh" << "-ic" << command;

    QProcess::startDetached(konsoleBinary(), args);

    return 0;
}

static int cmdAdoptRunning(SessionStore &store, LiveRegistry &registry) {
    int n = 0;

    for (const LiveEntry &e : registry.bySessionID()) {
        if (store.findByClaudeSession(e.sessionID))
            continue;

        Chat c = Chat::create("claude");
        c.claudeSessionID = e.sessionID;
        c.cwd = e.cwd;
        c.title = e.name;
        c.preview = TranscriptIndex::previewForSession(e.sessionID);
        c.lastActiveAt = e.updatedAt;
        store.add(c);
        ++n;
    }

    printf("adopted %d running session(s)\n", n);

    return 0;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("rezoom"));

    const QStringList args = app.arguments();
    const QString cmd = args.value(1);

    SessionStore store;
    Templates templates;
    LiveRegistry registry;

    if (cmd == "list" || cmd.isEmpty())
        return cmdList(store, registry);

    if (cmd == "resume" && args.size() >= 3)
        return cmdResume(store, registry, templates, args[2], args.contains("--print"));

    if (cmd == "adopt-running")
        return cmdAdoptRunning(store, registry);

    fprintf(stderr, "usage: rezoom-cli [list | resume <query> [--print] | adopt-running]\n");

    return 64;
}
