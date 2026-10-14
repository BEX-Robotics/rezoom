#!/bin/bash
# publish-apt.sh — add .debs to a signed apt repository directory (the
# gh-pages branch, served at https://bex-robotics.github.io/rezoom) and
# re-sign it with the BEX archive key. Machines then install with:
#   deb [signed-by=/etc/apt/keyrings/bex.asc] https://bex-robotics.github.io/rezoom stable main
#
# Usage: scripts/publish-apt.sh <repo-dir> <deb>...
# Signs with the secret key in the current GnuPG keyring (CI imports it from
# the BEX_APT_SIGNING_KEY secret). Keeps the newest KEEP versions per package.
set -euo pipefail

SITE=$1
shift
SUITE=stable
COMPONENT=main
KEEP=3

POOL=pool/$COMPONENT
mkdir -p "$SITE/$POOL"

for deb in "$@"; do
    name=$(dpkg-deb -f "$deb" Package)
    version=$(dpkg-deb -f "$deb" Version)
    arch=$(dpkg-deb -f "$deb" Architecture)
    cp "$deb" "$SITE/$POOL/${name}_${version}_${arch}.deb"
done

cd "$SITE"

# Older versions only cost space: apt always picks the newest. Debian names
# and versions never contain "_", so name_version_arch.deb splits cleanly.
for kind in $(ls $POOL | sed 's/_.*_/_/' | sort -u); do
    ls $POOL/${kind%%_*}_*_${kind#*_} | sort -V | head -n -$KEEP | xargs -r rm -v
done

rm -rf "dists/$SUITE"
ARCHES=$(ls $POOL | sed 's/.*_\([^_]*\)\.deb$/\1/' | sort -u)

for arch in $ARCHES; do
    dir=dists/$SUITE/$COMPONENT/binary-$arch
    mkdir -p "$dir"
    dpkg-scanpackages --arch "$arch" --multiversion $POOL > "$dir/Packages" 2>/dev/null
    gzip -9kn "$dir/Packages"
done

{
    echo "Origin: BEX"
    echo "Label: BEX"
    echo "Suite: $SUITE"
    echo "Codename: $SUITE"
    echo "Date: $(LC_ALL=C date -Ru)"
    echo "Architectures: $(echo $ARCHES)"
    echo "Components: $COMPONENT"
    echo "Description: BEX Robotics packages"
    echo "SHA256:"
    for f in $(cd "dists/$SUITE" && find "$COMPONENT" -type f | sort); do
        echo " $(sha256sum < "dists/$SUITE/$f" | cut -d' ' -f1) $(stat -c %s "dists/$SUITE/$f") $f"
    done
} > "dists/$SUITE/Release"

gpg --batch --yes --clearsign -o "dists/$SUITE/InRelease" "dists/$SUITE/Release"
gpg --batch --yes -abs -o "dists/$SUITE/Release.gpg" "dists/$SUITE/Release"
gpg --batch --yes --armor --export > bex.asc
touch .nojekyll

# Whoever opens the repo URL in a browser gets the instructions, not a 404.
cat > index.html <<'EOF_HTML'
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Rezoom apt repository</title>
<style>
  :root { color-scheme: light dark; }
  body { font: 16px/1.5 system-ui, sans-serif; max-width: 46rem; margin: 3rem auto; padding: 0 1rem; }
  pre { background: #8882; padding: 1rem; overflow-x: auto; border-radius: 6px; }
</style>
<h1>Rezoom apt repository</h1>
<p>Signed packages of <a href="https://github.com/BEX-Robotics/rezoom">Rezoom</a> for
Debian/Ubuntu 25.04 or newer (KF6). Add it once; <code>apt upgrade</code> brings every
new release.</p>
<pre>sudo curl -fsSLo /etc/apt/keyrings/bex.asc https://bex-robotics.github.io/rezoom/bex.asc
echo "deb [signed-by=/etc/apt/keyrings/bex.asc] https://bex-robotics.github.io/rezoom stable main" |
    sudo tee /etc/apt/sources.list.d/rezoom.list
sudo apt update &amp;&amp; sudo apt install rezoom</pre>
EOF_HTML
echo "published: $(ls $POOL | tr '\n' ' ')"
