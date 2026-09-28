#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>

#include "processscout.h"
#include "sessionhealth.h"
#include "transcriptindex.h"

SessionHealth::Health SessionHealth::of(const LiveEntry &e) {
    Health h = {};

    if (ProcessScout::isStopped(e.pid)) {
        h.state = State::Suspended;
        h.sinceMs = e.updatedAt; // its last registry write = when it froze
        return h;
    }

    if (e.status != QLatin1String("busy"))
        return h;

    const QString path = TranscriptIndex::pathForSession(e.sessionID);

    if (path.isEmpty())
        return h;

    const qint64 lastWrite = QFileInfo(path).lastModified().toMSecsSinceEpoch();

    if (QDateTime::currentMSecsSinceEpoch() - lastWrite > stalledAfter_ms) {
        h.state = State::Stalled;
        h.sinceMs = lastWrite;
    }

    return h;
}

QString SessionHealth::ageText(qint64 sinceMs) {
    const qint64 mins = (QDateTime::currentMSecsSinceEpoch() - sinceMs) / 60000;

    if (mins < 60)
        return QCoreApplication::translate("SessionHealth", "%n min", 0, int(qMax<qint64>(mins, 1)));

    if (mins < 48 * 60)
        return QCoreApplication::translate("SessionHealth", "%n h", 0, int(mins / 60));

    return QCoreApplication::translate("SessionHealth", "%n days", 0, int(mins / (24 * 60)));
}
