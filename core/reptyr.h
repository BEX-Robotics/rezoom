#pragma once
#include <QString>

// Whether the live session-move (reptyr) feature can run here, decided up
// front so a move is never attempted when it would only fail. reptyr is
// Linux-only; on macOS supported() is false and the feature is hidden.
namespace Reptyr {

// The reptyr invocation for a pid — always -T (steal the whole terminal
// from its emulator). Plain `reptyr <pid>` refuses any process sharing its
// process group, and claude's group membership changes as it spawns tools,
// so a children check at click time is not a reliable predictor.
QString command(int pid);

// A live reptyr process currently attached to pid (its last argv).
bool holding(int pid);

struct Status {
    bool ready = false;    // an attempt would work right now
    QString reason;        // human-readable state ("ready", "not installed", ...)
    QString fixCommand;    // command (sans pkexec/sudo) to make it ready, or empty
};

bool supported();  // platform could ever use reptyr (Linux yes, macOS no)
Status status();
}
