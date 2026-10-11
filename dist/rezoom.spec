Name:           rezoom
Version:        1.2.0
Release:        1%{?dist}
Summary:        Chat-list organizer for AI coding sessions (Claude Code, Codex) and terminals
License:        MIT
URL:            https://github.com/BEX-Robotics/rezoom
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++ cmake ninja-build extra-cmake-modules
BuildRequires:  qt6-qtbase-devel kf6-kparts-devel kf6-kcoreaddons-devel
# konsolepart provides the embedded terminal at runtime
Requires:       konsole-part
Requires:       konsole
Recommends:     tmux reptyr jq

%description
Chat-list organizer for Claude Code sessions and terminals: live presence,
embedded Konsole terminals, one-click resume, adoption of external sessions,
floating group windows, archiving. Every session stays forever resumable.

%prep
%autosetup

%build
%cmake -G Ninja
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%{_bindir}/rezoom
%{_bindir}/rezoom-cli
%{_bindir}/rezoom-notify-hook
%{_datadir}/applications/rezoom.desktop
%{_datadir}/icons/hicolor/scalable/apps/rezoom.svg

%changelog
* Sun Oct 11 2026 Pavel <254324686+pavel-bex@users.noreply.github.com> - 1.2.0-1
- Claude's orange spinner while working; green means your turn
- Agent names (bex-6b) on rows, in search and resume; one-click beam-in per row
- External ssh sessions as chats; suspended/stalled health; Reconnect bar
- Optional multi-account zones; safer beam-in (old window handled)

* Sat Sep 26 2026 Pavel <254324686+pavel-bex@users.noreply.github.com> - 1.1.0-1
- Live titles from Konsole, live output previews, codex sessions
- Beam-in/pop-out via reptyr -T, restart session, built-in window raise
- One-click freeze detection, opt-in trust pre-accept, stable ordering

* Thu Aug 20 2026 Pavel <254324686+pavel-bex@users.noreply.github.com> - 1.0.0-1
- First stable release: presence, embedded terminals, adoption, floating
  groups, auto-adopt, auto-resume, freeze detection, live pull via reptyr

* Tue Aug 19 2026 Pavel <254324686+pavel-bex@users.noreply.github.com> - 0.1.0-1
- Initial package
