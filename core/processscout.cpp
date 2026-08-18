#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include "processscout.h"

namespace ProcessScout {

QList<int> children(int pid) {
    QList<int> out;
    QFile f(QStringLiteral("/proc/%1/task/%1/children").arg(pid));

    if (!f.open(QIODevice::ReadOnly))
        return out;

    const QStringList parts = QString::fromUtf8(f.readAll()).split(' ', Qt::SkipEmptyParts);

    for (const QString &p : parts)
        out.append(p.toInt());

    return out;
}

QList<int> descendants(int pid) {
    QList<int> out;
    QList<int> queue = children(pid);

    while (!queue.isEmpty() && out.size() < 200) {
        const int p = queue.takeFirst();
        out.append(p);
        queue.append(children(p));
    }

    return out;
}

QString comm(int pid) {
    QFile f(QStringLiteral("/proc/%1/comm").arg(pid));

    if (!f.open(QIODevice::ReadOnly))
        return {};

    return QString::fromUtf8(f.readAll()).trimmed();
}

QStringList cmdline(int pid) {
    QFile f(QStringLiteral("/proc/%1/cmdline").arg(pid));

    if (!f.open(QIODevice::ReadOnly))
        return {};

    QStringList parts;
    const QList<QByteArray> raw = f.readAll().split(0);

    for (const QByteArray &b : raw)
        if (!b.isEmpty())
            parts.append(QString::fromUtf8(b));

    return parts;
}

QList<ProcInfo> findDescendants(int shellPID, const QStringList &names) {
    QList<ProcInfo> out;
    const QList<int> all = descendants(shellPID);

    for (int p : all) {
        const QString c = comm(p);

        if (names.contains(c))
            out.append({p, c, cmdline(p)});
    }

    return out;
}

QList<TmuxSession> tmuxSessions() {
    QList<TmuxSession> out;
    QProcess proc;
    proc.start("tmux", {"ls", "-F", "#{session_name}\t#{session_attached}"});

    if (!proc.waitForFinished(2000) || proc.exitCode())
        return out;

    const QString stdout_ = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = stdout_.split('\n', Qt::SkipEmptyParts);

    for (const QString &l : lines) {
        const QStringList parts = l.split('\t');

        if (!parts.isEmpty())
            out.append({parts[0], parts.size() > 1 && parts[1].toInt() > 0});
    }

    return out;
}

QList<ProcInfo> runningSsh() {
    QList<ProcInfo> out;
    const uint uid = getuid();
    const QDir proc("/proc");
    const QStringList procEntries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &e : procEntries) {
        bool ok = false;
        const int pid = e.toInt(&ok);

        if (!ok)
            continue;

        if (QFileInfo(proc.filePath(e)).ownerId() != uid)
            continue;

        if (comm(pid) == "ssh")
            out.append({pid, "ssh", cmdline(pid)});
    }

    return out;
}

QString sshDestination(const QStringList &cmdline) {
    // ssh options that consume the next token.
    static const QStringList argOpts = {"-p", "-i", "-l", "-o", "-L", "-R", "-D",
                                        "-F", "-J", "-W", "-b", "-c", "-e", "-m",
                                        "-O", "-Q", "-S", "-w", "-B", "-E", "-I"};

    for (int i = 1; i < cmdline.size(); ++i) {
        const QString &t = cmdline[i];

        if (argOpts.contains(t)) {
            ++i;
            continue;
        }

        if (t.startsWith('-'))
            continue;

        return t; // first non-option token = destination
    }

    return {};
}

} // namespace ProcessScout
