#pragma once

// Bring the Konsole window (and tab) hosting a process to the front: find
// the tab over Konsole's DBus, select it, then ask KWin to activate the
// window through a throwaway KWin script. No-op without a session bus.
namespace WindowRaiser {

bool raise(int pid);
}
