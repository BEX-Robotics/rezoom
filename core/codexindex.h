#pragma once
#include <QList>

#include "transcriptindex.h"

// Read-only view over ~/.codex/sessions/YYYY/MM/DD/rollout-<ts>-<uuid>.jsonl —
// OpenAI Codex CLI sessions, resumable via `codex resume <uuid>`. Same shape
// as TranscriptIndex so codex chats ride the same adoption/resume paths.
namespace CodexIndex {

// All codex sessions, newest first.
QList<TranscriptInfo> scanAll();

// The most recently modified session, optionally filtered by cwd — used to
// bind a codex process spotted inside a pane to its session id.
TranscriptInfo newestSession(const QString &cwdFilter);

TranscriptInfo readInfo(const QString &path);
}
