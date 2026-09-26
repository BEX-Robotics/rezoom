#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include "processscout.h"
#include "reptyr.h"

#ifdef Q_OS_LINUX
#include <sys/xattr.h>
#endif

namespace Reptyr {

bool supported() {
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

#ifdef Q_OS_LINUX

// Yama ptrace policy: 0 = unrestricted, >0 = restricted (need the capability).
static int ptraceScope() {
    QFile f(QStringLiteral("/proc/sys/kernel/yama/ptrace_scope"));

    if (!f.open(QIODevice::ReadOnly))
        return 0; // no Yama LSM — ptrace is unrestricted

    return f.readAll().trimmed().toInt();
}

// A file capability set on the binary (e.g. cap_sys_ptrace via setcap) lets
// reptyr ptrace even under a restrictive scope.
static bool hasFileCapability(const QString &path) {
    char buf[64];
    const ssize_t n =
        getxattr(path.toLocal8Bit().constData(), "security.capability", buf, sizeof(buf));
    return n > 0;
}

Status status() {
    Status s = {};
    const QString path = QStandardPaths::findExecutable(QStringLiteral("reptyr"));

    if (path.isEmpty()) {
        s.reason = QCoreApplication::translate("Reptyr", "reptyr is not installed");
        s.fixCommand = QStringLiteral("apt-get install -y reptyr");
        return s;
    }

    if (ptraceScope() == 0 || hasFileCapability(path)) {
        s.ready = true;
        s.reason = QCoreApplication::translate("Reptyr", "ready");
        return s;
    }

    s.reason = QCoreApplication::translate("Reptyr", "installed, but ptrace is restricted");
    s.fixCommand = QStringLiteral("setcap cap_sys_ptrace+ep %1").arg(path);
    return s;
}

#else

Status status() {
    Status s = {};
    s.reason = QCoreApplication::translate("Reptyr", "live moves are Linux-only");
    return s;
}

#endif

} // namespace Reptyr

QString Reptyr::command(int pid) {
    return QStringLiteral("reptyr -T %1").arg(pid);
}

bool Reptyr::holding(int pid) {
    const QString want = QString::number(pid);

    for (const auto &p : ProcessScout::byComm(QStringLiteral("reptyr")))
        if (p.cmdline.value(p.cmdline.size() - 1) == want)
            return true;

    return false;
}
