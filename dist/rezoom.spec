Name:           rezoom
Version:        0.1.0
Release:        1%{?dist}
Summary:        WhatsApp-style organizer for Claude Code sessions and terminals
License:        MIT
URL:            https://github.com/pavel-bex/rezoom
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  gcc-c++ cmake ninja-build extra-cmake-modules
BuildRequires:  qt6-qtbase-devel kf6-kparts-devel kf6-kcoreaddons-devel
# konsolepart provides the embedded terminal at runtime
Requires:       konsole-part
Requires:       konsole
Recommends:     zsh tmux

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
%{_bindir}/rezoom
%{_bindir}/rezoom-cli
%{_datadir}/applications/rezoom.desktop
%{_datadir}/icons/hicolor/scalable/apps/rezoom.svg

%changelog
* Tue Aug 19 2026 Pavel <bugpwr@gmail.com> - 0.1.0-1
- Initial package
