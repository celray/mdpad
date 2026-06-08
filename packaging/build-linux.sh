#!/usr/bin/env bash
# Build the mdpad Linux release packages: a portable tarball and a .deb.
#
# Assumes the Release binary is already built:
#     cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Produces, under dist/:
#     mdpad-<ver>-linux-<arch>.tar.gz   self-contained, run from the folder
#     mdpad_<ver>_<debarch>.deb         installs to /opt, registers .md MIME
#
# Architecture is taken from the running machine, so the same script builds
# the aarch64 packages when run on an ARM64 host.
set -euo pipefail

VERSION="${VERSION:-1.1}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/build/bin"
PKG="$ROOT/packaging"
DIST="$ROOT/dist"
SVG="$ROOT/assets/mdpad.svg"

[ -x "$BIN/mdpad" ] || { echo "error: $BIN/mdpad not found — build Release first" >&2; exit 1; }
command -v rsvg-convert >/dev/null || { echo "error: rsvg-convert required for icons" >&2; exit 1; }

MACH="$(uname -m)"
case "$MACH" in
  x86_64)  ARCH=x86_64;  DEBARCH=amd64 ;;
  aarch64) ARCH=aarch64; DEBARCH=arm64 ;;
  *) echo "error: unsupported arch $MACH" >&2; exit 1 ;;
esac

ICON_SIZES="16 24 32 48 64 128 256 512"

rm -rf "$DIST"; mkdir -p "$DIST"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# --- render icons once, reused by both packages ---
icons="$work/icons"; mkdir -p "$icons"
for s in $ICON_SIZES; do
  rsvg-convert -w "$s" -h "$s" "$SVG" -o "$icons/${s}.png"
done

# helper: lay out a freedesktop share/ tree under $1
make_share() {
  local dest="$1"
  install -Dm644 "$PKG/mdpad.desktop" "$dest/applications/mdpad.desktop"
  install -Dm644 "$PKG/mdpad.xml"     "$dest/mime/packages/mdpad.xml"
  install -Dm644 "$SVG" "$dest/icons/hicolor/scalable/apps/mdpad.svg"
  for s in $ICON_SIZES; do
    install -Dm644 "$icons/${s}.png" "$dest/icons/hicolor/${s}x${s}/apps/mdpad.png"
  done
}

# ===========================================================================
# Portable tarball
# ===========================================================================
TB="mdpad-$VERSION-linux-$ARCH"
tb="$work/$TB"; mkdir -p "$tb"
cp -a "$BIN/mdpad" "$BIN"/libSDL3.so* "$BIN"/libSDL3_ttf.so* "$BIN/assets" "$tb/"
strip "$tb/mdpad" 2>/dev/null || true
make_share "$tb/share"

cat > "$tb/README.txt" <<EOF
mdpad $VERSION - lightweight markdown viewer (Linux $ARCH)

Run:
    ./mdpad [file.md ...]

Everything needed is in this folder (SDL3 + fonts bundled). Keep the files
together; the binary loads its fonts and libraries from this directory.

Install for your user (no root) - adds a menu entry, icon, and makes mdpad
the default opener for Markdown files:
    ./install.sh

Undo it:
    ./uninstall.sh
EOF

cat > "$tb/install.sh" <<'EOF'
#!/usr/bin/env sh
# Install mdpad into ~/.local (no root). Re-run to update.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
LIB="$HOME/.local/lib/mdpad"
DATA="$HOME/.local/share"
mkdir -p "$LIB" "$HOME/.local/bin"
cp -a "$HERE/mdpad" "$HERE"/libSDL3.so* "$HERE"/libSDL3_ttf.so* "$HERE/assets" "$LIB/"
ln -sf "$LIB/mdpad" "$HOME/.local/bin/mdpad"
cp -a "$HERE/share/." "$DATA/"
update-mime-database "$DATA/mime" >/dev/null 2>&1 || true
update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true
gtk-update-icon-cache -q -t -f "$DATA/icons/hicolor" >/dev/null 2>&1 || true
xdg-mime default mdpad.desktop text/markdown >/dev/null 2>&1 || true
echo "Installed. Ensure ~/.local/bin is on your PATH, then run: mdpad"
echo "Markdown files now open with mdpad (you may need to log out/in once)."
EOF
chmod +x "$tb/install.sh"

cat > "$tb/uninstall.sh" <<'EOF'
#!/usr/bin/env sh
set -e
DATA="$HOME/.local/share"
rm -rf "$HOME/.local/lib/mdpad" "$HOME/.local/bin/mdpad"
rm -f "$DATA/applications/mdpad.desktop" "$DATA/mime/packages/mdpad.xml" \
      "$DATA/icons/hicolor/scalable/apps/mdpad.svg"
for s in 16 24 32 48 64 128 256 512; do
  rm -f "$DATA/icons/hicolor/${s}x${s}/apps/mdpad.png"
done
update-mime-database "$DATA/mime" >/dev/null 2>&1 || true
update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true
echo "Removed mdpad from ~/.local."
EOF
chmod +x "$tb/uninstall.sh"

( cd "$work" && tar czf "$DIST/$TB.tar.gz" "$TB" )

# ===========================================================================
# Debian package (.deb), assembled with ar (no dpkg-deb needed)
# ===========================================================================
deb="$work/deb"
mkdir -p "$deb/opt/mdpad" "$deb/usr/bin" "$deb/DEBIAN"
cp -a "$BIN/mdpad" "$BIN"/libSDL3.so* "$BIN"/libSDL3_ttf.so* "$BIN/assets" "$deb/opt/mdpad/"
strip "$deb/opt/mdpad/mdpad" 2>/dev/null || true
printf '#!/bin/sh\nexec /opt/mdpad/mdpad "$@"\n' > "$deb/usr/bin/mdpad"
chmod 755 "$deb/usr/bin/mdpad"
make_share "$deb/usr/share"

INSTALLED=$(du -sk "$deb/opt" "$deb/usr" | awk '{s+=$1} END{print s}')
cat > "$deb/DEBIAN/control" <<EOF
Package: mdpad
Version: $VERSION
Section: utils
Priority: optional
Architecture: $DEBARCH
Maintainer: celray <celray.chawanda@gmail.com>
Installed-Size: $INSTALLED
Depends: libc6
Recommends: libx11-6, libwayland-client0, libxkbcommon0
Homepage: https://github.com/celray/mdpad
Description: Lightweight cross-platform markdown viewer
 mdpad renders Markdown with infinite-scroll, built on SDL3, SDL_ttf and md4c.
 SDL3 and fonts are bundled, so it runs on most distributions out of the box.
 Registers itself as a handler for Markdown files.
EOF

cat > "$deb/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ]; then
  update-desktop-database -q /usr/share/applications 2>/dev/null || true
  update-mime-database /usr/share/mime 2>/dev/null || true
  gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true
fi
EOF
cat > "$deb/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "remove" ] || [ "$1" = "purge" ]; then
  update-desktop-database -q /usr/share/applications 2>/dev/null || true
  update-mime-database /usr/share/mime 2>/dev/null || true
  gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true
fi
EOF
chmod 755 "$deb/DEBIAN/postinst" "$deb/DEBIAN/postrm"

( cd "$deb"
  echo "2.0" > debian-binary
  tar czf control.tar.gz -C DEBIAN .
  tar czf data.tar.gz --owner=0 --group=0 opt usr
  ar rc "$DIST/mdpad_${VERSION}_${DEBARCH}.deb" debian-binary control.tar.gz data.tar.gz
)

echo "Built in dist/:"
ls -lh "$DIST"
