#pragma once
#include <QString>

#include "liveregistry.h"

// What's really going on with a live session, beyond what claude's own
// registry says. The registry can't be trusted for two cases: a process
// stopped by Ctrl+Z can't update its status (it stays "busy" forever), and
// a hung turn looks exactly like a long one. The kernel answers the first
// precisely; only the second needs a threshold.
namespace SessionHealth {

enum class State {
    Normal,
    Suspended, // stopped by a signal (Ctrl+Z) — alive but frozen
    Stalled,   // "busy", but its transcript hasn't moved for a long time
};

struct Health {
    State state = State::Normal;
    qint64 sinceMs = 0; // when it froze / last made progress (epoch ms)
};

// Long tool runs still write progress to the transcript; two silent hours
// while "busy" is past any normal turn.
constexpr qint64 stalledAfter_ms = 2 * 3600 * 1000;

Health of(const LiveEntry &e);

// "3 min" / "5 h" / "21 days" since the given moment.
QString ageText(qint64 sinceMs);
}
