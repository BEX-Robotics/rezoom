#!/bin/bash
# make-deb.sh — package rezoom as a .deb that depends on distro packages
# (Qt6/KF6/konsole-kpart from apt) instead of bundling them. Spread it to
# other machines with: sudo apt install ./rezoom_<version>_amd64.deb
set -euo pipefail

cd "$(dirname "$0")/.."
VERSION=$(grep -oP 'project\(rezoom VERSION \K[0-9.]+' CMakeLists.txt)
ARCH=$(dpkg --print-architecture)
BUILD=build
PKG=dist/pkg

[ -x "$BUILD/rezoom" ] || { echo "build first: cmake -B build -G Ninja && ninja -C build"; exit 1; }

rm -rf "$PKG"
mkdir -p "$PKG/DEBIAN" "$PKG/usr/bin" "$PKG/usr/share/applications" "$PKG/usr/share/icons/hicolor/scalable/apps"
cp "$BUILD/rezoom" "$BUILD/rezoom-cli" "$PKG/usr/bin/"
cp dist/rezoom.desktop "$PKG/usr/share/applications/"
cp dist/rezoom.svg "$PKG/usr/share/icons/hicolor/scalable/apps/"

cat > "$PKG/DEBIAN/control" <<EOF
Package: rezoom
Version: $VERSION
Architecture: $ARCH
Maintainer: Pavel <bugpwr@gmail.com>
Depends: libqt6widgets6, libqt6core6 | libqt6core6t64, libkf6parts6, libkf6coreaddons6, konsole-kpart, konsole
Recommends: zsh, tmux
Section: utils
Priority: optional
Description: WhatsApp-style organizer for Claude Code sessions and terminals
 Left-panel chat list of claude/ssh/tmux/shell sessions with live presence,
 embedded Konsole terminals, one-click resume, adoption of external sessions,
 and archiving. Sessions stay forever resumable.
EOF

OUT="dist/rezoom_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKG" "$OUT"
rm -rf "$PKG"
echo "built $OUT"
