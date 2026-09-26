#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "transcriptindex.h"
#include "zones.h"

static QString projectsDir(const Zones::Zone &z) {
    return z.dir + "/projects";
}

// Minimal unescape of a JSON string fragment, for display only.
static QString unescapePreview(QString s) {
    s.replace("\\n", " ");
    s.replace("\\t", " ");
    s.replace("\\\"", "\"");
    s.replace("\\\\", "\\");
    s.replace(QRegularExpression("\\\\u[0-9a-fA-F]{4}"), " ");

    return s.simplified();
}

static bool isNoisePreview(const QString &p) {
    return p.startsWith('<') || p.startsWith("Caveat:") || p.startsWith("[Request interrupted");
}

static QString extractPreview(const QString &head) {
    // First real user message. Content is either a plain string or an array
    // of blocks with {"type":"text","text":"..."}.
    static const QRegularExpression userStrRe(
        "\"type\":\"user\",\"message\":\\{\"role\":\"user\",\"content\":\"((?:[^\"\\\\]|\\\\.){1,300})");
    static const QRegularExpression userBlockRe(
        "\"role\":\"user\",\"content\":\\[\\{\"type\":\"text\",\"text\":\"((?:[^\"\\\\]|\\\\.){1,300})");

    for (const auto &re : {userStrRe, userBlockRe}) {
        auto it = re.globalMatch(head);

        while (it.hasNext()) {
            const QString cand = unescapePreview(it.next().captured(1));

            if (!cand.isEmpty() && !isNoisePreview(cand))
                return cand;
        }
    }

    return {};
}

TranscriptInfo TranscriptIndex::readInfo(const QString &path) {
    TranscriptInfo info = {};
    info.path = path;
    info.sessionID = QFileInfo(path).completeBaseName();
    const QFileInfo fi(path);
    info.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    info.size = fi.size();

    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return info;

    const QString head = QString::fromUtf8(f.read(256 * 1024));
    static const QRegularExpression cwdRe("\"cwd\":\"((?:[^\"\\\\]|\\\\.)+)\"");
    const auto m = cwdRe.match(head);

    if (m.hasMatch())
        info.cwd = unescapePreview(m.captured(1));

    info.preview = extractPreview(head);
    return info;
}

static void scanZone(const Zones::Zone &z, QList<TranscriptInfo> &out) {
    const QDir root(projectsDir(z));
    const QStringList projects = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &p : projects) {
        const QDir d(root.filePath(p));
        const QStringList files = d.entryList({"*.jsonl"}, QDir::Files);

        for (const QString &fn : files) {
            TranscriptInfo info = TranscriptIndex::readInfo(d.filePath(fn));
            info.zone = z.name;
            out.append(info);
        }
    }
}

QList<TranscriptInfo> TranscriptIndex::scanAll() {
    QList<TranscriptInfo> out;

    for (const Zones::Zone &z : Zones::all())
        scanZone(z, out);

    std::sort(out.begin(), out.end(), [](const TranscriptInfo &a, const TranscriptInfo &b) {
        return a.mtimeMs > b.mtimeMs;
    });

    return out;
}

QString TranscriptIndex::pathForSession(const QString &sessionID) {
    if (sessionID.isEmpty())
        return {};

    for (const Zones::Zone &z : Zones::all()) {
        const QDir root(projectsDir(z));
        const QStringList projects = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QString &p : projects) {
            const QString cand = root.filePath(p) + "/" + sessionID + ".jsonl";

            if (QFile::exists(cand))
                return cand;
        }
    }

    return {};
}

QString TranscriptIndex::lastMessagePreview(const QString &sessionID) {
    const QString path = pathForSession(sessionID);

    if (path.isEmpty())
        return {};

    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return {};

    const qint64 tailBytes = 64 * 1024;

    if (f.size() > tailBytes)
        f.seek(f.size() - tailBytes);

    const QString tail = QString::fromUtf8(f.readAll());
    static const QRegularExpression userStrRe(
        "\"type\":\"user\",\"message\":\\{\"role\":\"user\",\"content\":\"((?:[^\"\\\\]|\\\\.){1,300})");
    static const QRegularExpression textBlockRe(
        "\"type\":\"text\",\"text\":\"((?:[^\"\\\\]|\\\\.){1,300})");
    QString best;
    qsizetype bestPos = -1;

    for (const auto &re : {userStrRe, textBlockRe}) {
        auto it = re.globalMatch(tail);

        while (it.hasNext()) {
            const auto m = it.next();
            const QString cand = unescapePreview(m.captured(1));

            if (!cand.isEmpty() && !isNoisePreview(cand) && m.capturedStart(1) > bestPos) {
                best = cand;
                bestPos = m.capturedStart(1);
            }
        }
    }

    return best;
}

QString TranscriptIndex::previewForSession(const QString &sessionID) {
    const QString path = pathForSession(sessionID);

    if (path.isEmpty())
        return {};

    return readInfo(path).preview;
}
