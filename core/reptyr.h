#pragma once
#include <QString>

// Whether the live session-move (reptyr) feature can run here, decided up
// front so a move is never attempted when it would only fail. reptyr is
// Linux-only; on macOS supported() is false and the feature is hidden.
namespace Reptyr {

struct Status {
    bool ready = false;    // an attempt would work right now
    QString reason;        // human-readable state ("ready", "not installed", ...)
    QString fixCommand;    // command (sans pkexec/sudo) to make it ready, or empty
};

bool supported();  // platform could ever use reptyr (Linux yes, macOS no)
Status status();
}
