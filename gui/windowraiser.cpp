#include "windowraiser.h"

#ifdef REZOOM_HAVE_DBUS

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryFile>

#include "core/processscout.h"

static QDBusMessage konsoleCall(const QString &service, const QString &path,
                                const QString &iface, const QString &method,
                                const QVariantList &args = {}) {
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, iface, method);
    call.setArguments(args);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, 500);
}

static QStringList childNodes(const QString &service, const QString &path) {
    const QDBusMessage reply = konsoleCall(service, path,
        QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    static const QRegularExpression nodeRe("<node name=\"(\\d+)\"");
    QStringList out;
    auto it = nodeRe.globalMatch(reply.arguments().value(0).toString());

    while (it.hasNext())
        out << it.next().captured(1);

    return out;
}

static int intReply(const QDBusMessage &m) {
    return m.type() == QDBusMessage::ReplyMessage ? m.arguments().value(0).toInt() : 0;
}

// The tab whose foreground or shell process is pid or one of its ancestors.
static QString findSession(const QString &service, int pid) {
    QSet<int> chain = {pid};

    for (int p = pid; p > 1;) {
        p = ProcessScout::parentPid(p);
        chain.insert(p);
    }

    const QString iface = QStringLiteral("org.kde.konsole.Session");

    for (const QString &sid : childNodes(service, QStringLiteral("/Sessions"))) {
        const QString path = "/Sessions/" + sid;
        const int fg = intReply(konsoleCall(service, path, iface, QStringLiteral("foregroundProcessId")));
        const int shell = intReply(konsoleCall(service, path, iface, QStringLiteral("processId")));

        if (chain.contains(fg) || chain.contains(shell))
            return sid;
    }

    return {};
}

static void selectTab(const QString &service, const QString &session) {
    const QString iface = QStringLiteral("org.kde.konsole.Window");

    for (const QString &wid : childNodes(service, QStringLiteral("/Windows"))) {
        const QString path = "/Windows/" + wid;
        const QDBusMessage list = konsoleCall(service, path, iface, QStringLiteral("sessionList"));

        if (list.arguments().value(0).toStringList().contains(session)) {
            konsoleCall(service, path, iface, QStringLiteral("setCurrentSession"), {session});
            return;
        }
    }
}

// KWin can only be told to activate a window from inside a KWin script.
// Prefer the window whose caption carries the tab's title (a konsole
// process may own several windows), else any window of that process.
static bool activateViaKWin(int konsolePID, const QString &title) {
    QTemporaryFile js(QDir::tempPath() + "/rezoom-raise-XXXXXX.js");

    if (!js.open())
        return false;

    QString escaped = title;
    escaped.replace('\\', "\\\\").replace('\'', "\\'").replace('\n', ' ');
    js.write(QStringLiteral(
        "var pid = %1, title = '%2', pick = null;\n"
        "var ws = workspace.windowList();\n"
        "for (var i = 0; i < ws.length; ++i) {\n"
        "  if (ws[i].pid !== pid) continue;\n"
        "  if (!pick || (title && ws[i].caption.indexOf(title) >= 0)) pick = ws[i];\n"
        "}\n"
        "if (pick) { pick.minimized = false; workspace.activeWindow = pick; }\n")
        .arg(konsolePID).arg(escaped).toUtf8());
    js.flush();

    const QString kwin = QStringLiteral("org.kde.KWin");
    const QString iface = QStringLiteral("org.kde.kwin.Scripting");
    const QString plugin = QStringLiteral("rezoom-raise");
    konsoleCall(kwin, QStringLiteral("/Scripting"), iface, QStringLiteral("unloadScript"), {plugin});
    const QDBusMessage loaded = konsoleCall(kwin, QStringLiteral("/Scripting"), iface,
                                            QStringLiteral("loadScript"), {js.fileName(), plugin});

    if (loaded.type() != QDBusMessage::ReplyMessage || loaded.arguments().value(0).toInt() < 0)
        return false; // not KWin (or scripting disabled)

    const int id = loaded.arguments().value(0).toInt();

    konsoleCall(kwin, QStringLiteral("/Scripting/Script%1").arg(id),
                QStringLiteral("org.kde.kwin.Script"), QStringLiteral("run"));
    konsoleCall(kwin, QStringLiteral("/Scripting"), iface, QStringLiteral("unloadScript"), {plugin});

    return true;
}

// The suspended job's shell must be sitting at its prompt (the shell itself
// is the tab's foreground process) — otherwise "fg" would be typed into
// whatever program owns the terminal.
static QString promptSession(int pid, QString *service) {
    const int kpid = ProcessScout::ancestorPidOfComm(pid, QStringLiteral("konsole"));

    if (kpid <= 0)
        return {};

    *service = QStringLiteral("org.kde.konsole-%1").arg(kpid);
    const QString session = findSession(*service, pid);

    if (session.isEmpty())
        return {};

    const QString path = "/Sessions/" + session;
    const QString iface = QStringLiteral("org.kde.konsole.Session");
    const int fg = intReply(konsoleCall(*service, path, iface, QStringLiteral("foregroundProcessId")));
    const int shell = intReply(konsoleCall(*service, path, iface, QStringLiteral("processId")));

    return (fg > 0 && fg == shell) ? session : QString();
}

bool WindowRaiser::canContinue(int pid) {
    QString service;
    return QDBusConnection::sessionBus().isConnected() && !promptSession(pid, &service).isEmpty();
}

bool WindowRaiser::continueJob(int pid) {
    QString service;
    const QString session = promptSession(pid, &service);

    if (session.isEmpty())
        return false;

    // "fg %claude" picks the job by name, so other stopped jobs in that
    // shell stay put.
    const QString cmd = QStringLiteral("fg %%1\n").arg(ProcessScout::comm(pid));
    konsoleCall(service, "/Sessions/" + session, QStringLiteral("org.kde.konsole.Session"),
                QStringLiteral("sendText"), {cmd});
    raise(pid);

    return true;
}

bool WindowRaiser::canRaise(int pid) {
    return QDBusConnection::sessionBus().isConnected()
        && ProcessScout::ancestorPidOfComm(pid, QStringLiteral("konsole")) > 0;
}

bool WindowRaiser::raise(int pid) {
    const int kpid = ProcessScout::ancestorPidOfComm(pid, QStringLiteral("konsole"));

    if (kpid <= 0)
        return false;

    const QString service = QStringLiteral("org.kde.konsole-%1").arg(kpid);
    const QString session = findSession(service, pid);
    QString title;

    if (!session.isEmpty()) {
        selectTab(service, session);
        title = konsoleCall(service, "/Sessions/" + session,
                            QStringLiteral("org.kde.konsole.Session"),
                            QStringLiteral("title"), {1}).arguments().value(0).toString();
    }

    if (activateViaKWin(kpid, title))
        return true;

    // Not KWin: at least ask Konsole itself to come forward.
    konsoleCall(service, QStringLiteral("/org/kde/konsole"),
                QStringLiteral("org.freedesktop.Application"), QStringLiteral("Activate"),
                {QVariantMap()});
    return true;
}

#else

bool WindowRaiser::canContinue(int) {
    return false;
}

bool WindowRaiser::continueJob(int) {
    return false;
}

bool WindowRaiser::canRaise(int) {
    return false;
}

bool WindowRaiser::raise(int) {
    return false;
}

#endif
