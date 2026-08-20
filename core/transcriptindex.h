#pragma once
#include <QList>
#include <QString>

// Read-only view over ~/.claude/projects/*/<uuid>.jsonl — every claude session
// that ever ran on this machine. Used for adopting dead-but-resumable sessions.
struct TranscriptInfo {
    QString sessionID;
    QString cwd;
    QString preview; // first real user message, cleaned up
    QString path;
    qint64 mtimeMs = 0;
    qint64 size = 0;
};

namespace TranscriptIndex {

// All transcripts, newest first. Reads only the head of each file — fast.
QList<TranscriptInfo> scanAll();

// Locate a session's transcript by uuid across all project dirs.
QString pathForSession(const QString &sessionID);

QString previewForSession(const QString &sessionID);

// Most recent message text (user or assistant) from the transcript tail —
// the WhatsApp-style "last message" line.
QString lastMessagePreview(const QString &sessionID);
TranscriptInfo readInfo(const QString &path);
}
