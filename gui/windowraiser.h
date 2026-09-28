#pragma once

// Bring the Konsole window (and tab) hosting a process to the front: find
// the tab over Konsole's DBus, select it, then ask KWin to activate the
// window through a throwaway KWin script. No-op without a session bus.
namespace WindowRaiser {

bool raise(int pid);

// Whether raise() can work for this pid here: needs a session bus and the
// process living in a Konsole. Surfaces hide the action otherwise.
bool canRaise(int pid);

// A process suspended by Ctrl+Z in a Konsole tab: type "fg %<name>" into
// that tab's shell and raise the window. Only when the shell is at its
// prompt — canContinue() says whether that's the case right now.
bool canContinue(int pid);
bool continueJob(int pid);
}
