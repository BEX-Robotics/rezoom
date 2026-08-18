#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "transcriptindex.h"

static QString projectsDir() {
    return QDir::homePath() + "/.claude/projects";
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

QList<TranscriptInfo> TranscriptIndex::scanAll() {
    QList<TranscriptInfo> out;
    const QDir root(projectsDir());
    const QStringList projects = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &p : projects) {
        const QDir d(root.filePath(p));
        const QStringList files = d.entryList({"*.jsonl"}, QDir::Files);

        for (const QString &fn : files)
            out.append(readInfo(d.filePath(fn)));
    }

    std::sort(out.begin(), out.end(), [](const TranscriptInfo &a, const TranscriptInfo &b) {
        return a.mtimeMs > b.mtimeMs;
    });

    return out;
}

QString TranscriptIndex::pathForSession(const QString &sessionID) {
    if (sessionID.isEmpty())
        return {};

    const QDir root(projectsDir());
    const QStringList projects = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &p : projects) {
        const QString cand = root.filePath(p) + "/" + sessionID + ".jsonl";

        if (QFile::exists(cand))
            return cand;
    }

    return {};
}

QString TranscriptIndex::previewForSession(const QString &sessionID) {
    const QString path = pathForSession(sessionID);

    if (path.isEmpty())
        return {};

    return readInfo(path).preview;
}
