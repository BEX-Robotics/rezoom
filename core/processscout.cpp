#include <unistd.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>

#include "processscout.h"

#ifdef Q_OS_MACOS
#include <libproc.h>
#include <sys/sysctl.h>

#include <cstring>
#include <vector>
#endif

namespace ProcessScout {

#ifdef Q_OS_MACOS

// No /proc on macOS: snapshot the process table via sysctl and read names /
// argv via libproc / KERN_PROCARGS2.
static std::vector<kinfo_proc> allProcs() {
    int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
    size_t size = 0;

    if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0)
        return {};

    std::vector<kinfo_proc> procs(size / sizeof(kinfo_proc) + 16);
    size = procs.size() * sizeof(kinfo_proc);

    if (sysctl(mib, 3, procs.data(), &size, nullptr, 0) != 0)
        return {};

    procs.resize(size / sizeof(kinfo_proc));

    return procs;
}

QList<int> children(int pid) {
    QList<int> out;

    for (const kinfo_proc &p : allProcs())
        if (p.kp_eproc.e_ppid == pid)
            out.append(p.kp_proc.p_pid);

    return out;
}

QString comm(int pid) {
    char name[2 * MAXCOMLEN + 1] = {};

    if (proc_name(pid, name, sizeof(name)) <= 0)
        return {};

    return QString::fromUtf8(name);
}

QStringList cmdline(int pid) {
    int mib[3] = {CTL_KERN, KERN_PROCARGS2, pid};
    size_t size = 0;

    if (sysctl(mib, 3, nullptr, &size, nullptr, 0) != 0 || size < sizeof(int))
        return {};

    std::vector<char> buf(size);

    if (sysctl(mib, 3, buf.data(), &size, nullptr, 0) != 0 || size < sizeof(int))
        return {};

    // Layout: argc, exec_path\0, padding \0s, argv[0]\0 argv[1]\0 ...
    int argc = 0;
    memcpy(&argc, buf.data(), sizeof(int));

    const char *p = buf.data() + sizeof(int);
    const char *end = buf.data() + size;

    while (p < end && *p) // skip exec_path
        ++p;

    while (p < end && !*p) // skip padding
        ++p;

    QStringList parts;

    for (int i = 0; i < argc && p < end; ++i) {
        const QString arg = QString::fromUtf8(p);
        parts.append(arg);
        p += strlen(p) + 1;
    }

    return parts;
}

QList<ProcInfo> runningSsh() {
    QList<ProcInfo> out;
    const uid_t uid = getuid();

    for (const kinfo_proc &p : allProcs()) {
        if (p.kp_eproc.e_pcred.p_ruid != uid)
            continue;

        const int pid = p.kp_proc.p_pid;

        if (comm(pid) == "ssh")
            out.append({pid, "ssh", cmdline(pid)});
    }

    return out;
}

#else

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

#endif

QString tty(int pid) {
#ifdef Q_OS_MACOS
    Q_UNUSED(pid);

    return {};
#else
    return QFile::symLinkTarget(QStringLiteral("/proc/%1/fd/0").arg(pid));
#endif
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

#ifndef Q_OS_MACOS

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

#endif

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

#ifndef Q_OS_MACOS

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

#endif

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
