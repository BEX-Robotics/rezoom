#include <algorithm>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "codexindex.h"

static QString codexSessionsDir() {
    const QString override = qEnvironmentVariable("REZOOM_CODEX_DIR");

    return (override.isEmpty() ? QDir::homePath() + "/.codex" : override) + "/sessions";
}

// Minimal unescape for display, same spirit as TranscriptIndex.
static QString unescape(QString s) {
    s.replace("\\n", " ");
    s.replace("\\t", " ");
    s.replace("\\\"", "\"");
    s.replace("\\\\", "\\");

    return s.simplified();
}

TranscriptInfo CodexIndex::readInfo(const QString &path) {
    TranscriptInfo info = {};
    info.path = path;
    const QFileInfo fi(path);
    info.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    info.size = fi.size();

    // rollout-2026-07-13T14-55-43-<uuid>.jsonl — the uuid is the session id.
    static const QRegularExpression uuidRe(
        "([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})\\.jsonl$");
    const auto um = uuidRe.match(path);

    if (um.hasMatch())
        info.sessionID = um.captured(1);

    QFile f(path);

    if (!f.open(QIODevice::ReadOnly))
        return info;

    const QString head = QString::fromUtf8(f.read(64 * 1024));
    static const QRegularExpression cwdRe("\"cwd\":\"((?:[^\"\\\\]|\\\\.)+)\"");
    const auto cm = cwdRe.match(head);

    if (cm.hasMatch())
        info.cwd = unescape(cm.captured(1));

    // First real user message; the leading <environment_context> block is noise.
    static const QRegularExpression userRe(
        "\"role\":\"user\",\"content\":\\[\\{\"type\":\"input_text\","
        "\"text\":\"((?:[^\"\\\\]|\\\\.){1,300})");
    auto it = userRe.globalMatch(head);

    while (it.hasNext()) {
        const QString cand = unescape(it.next().captured(1));

        if (!cand.isEmpty() && !cand.startsWith('<')) {
            info.preview = cand;
            break;
        }
    }

    return info;
}

QList<TranscriptInfo> CodexIndex::scanAll() {
    QList<TranscriptInfo> out;
    QDirIterator it(codexSessionsDir(), {"*.jsonl"}, QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext())
        out.append(readInfo(it.next()));

    std::sort(out.begin(), out.end(), [](const TranscriptInfo &a, const TranscriptInfo &b) {
        return a.mtimeMs > b.mtimeMs;
    });

    return out;
}

TranscriptInfo CodexIndex::newestSession(const QString &cwdFilter) {
    const QList<TranscriptInfo> all = scanAll();

    for (const TranscriptInfo &t : all)
        if (cwdFilter.isEmpty() || t.cwd == cwdFilter)
            return t;

    return all.isEmpty() ? TranscriptInfo{} : all.first();
}
