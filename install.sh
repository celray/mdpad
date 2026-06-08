#!/bin/sh
# mdpad installer
#
#   curl -fsSL https://raw.githubusercontent.com/celray/mdpad/master/install.sh | sh
#
# Downloads the latest mdpad release for your Linux machine and installs it into
# ~/.local, with no root and no system packages. Re-run any time to update.
#
# Knobs (set as environment variables before running):
#   MDPAD_PREFIX   install root            (default: ~/.local)
#   MDPAD_VERSION  release tag to install  (default: the latest release)
#
#   curl -fsSL .../install.sh | MDPAD_PREFIX=/opt/mdpad-local sh
#   curl -fsSL .../install.sh | MDPAD_VERSION=1.0 sh
set -eu

REPO="celray/mdpad"
PREFIX="${MDPAD_PREFIX:-$HOME/.local}"

say() { printf 'mdpad: %s\n' "$*"; }
die() { printf 'mdpad: error: %s\n' "$*" >&2; exit 1; }

# --- platform check ---------------------------------------------------------
[ "$(uname -s)" = "Linux" ] ||
  die "only Linux builds are published; on other systems build from source: https://celray.github.io/mdpad/building/"
case "$(uname -m)" in
  x86_64 | amd64)  ARCH=x86_64 ;;
  aarch64 | arm64) ARCH=aarch64 ;;
  *) die "unsupported architecture: $(uname -m)" ;;
esac

# --- pick a downloader ------------------------------------------------------
if command -v curl >/dev/null 2>&1; then
  fetch()    { curl -fsSL "$1"; }
  download() { curl -fSL --progress-bar "$1" -o "$2"; }
elif command -v wget >/dev/null 2>&1; then
  fetch()    { wget -qO- "$1"; }
  download() { wget -q --show-progress -O "$2" "$1"; }
else
  die "need either curl or wget to download the release"
fi

# --- find the release tarball for this architecture -------------------------
if [ -n "${MDPAD_VERSION:-}" ]; then
  api="https://api.github.com/repos/$REPO/releases/tags/v$MDPAD_VERSION"
  say "looking up release v$MDPAD_VERSION..."
else
  api="https://api.github.com/repos/$REPO/releases/latest"
  say "looking up the latest release..."
fi

url="$(fetch "$api" \
  | grep '"browser_download_url"' \
  | grep "linux-$ARCH.tar.gz" \
  | head -n1 \
  | cut -d'"' -f4)"
[ -n "$url" ] || die "no Linux $ARCH tarball found on that release (ARM64 may not be published yet)"

# --- download and unpack ----------------------------------------------------
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
say "downloading $(basename "$url")..."
download "$url" "$tmp/mdpad.tar.gz"
tar xzf "$tmp/mdpad.tar.gz" -C "$tmp"
src="$(find "$tmp" -maxdepth 1 -type d -name 'mdpad-*' | head -n1)"
[ -n "$src" ] && [ -x "$src/mdpad" ] || die "the downloaded archive is not laid out as expected"

# --- install into PREFIX (mirrors the tarball's bundled install.sh) ---------
LIB="$PREFIX/lib/mdpad"
BIN="$PREFIX/bin"
DATA="$PREFIX/share"
say "installing into $PREFIX..."
mkdir -p "$LIB" "$BIN"
cp -a "$src/mdpad" "$src"/libSDL3.so* "$src"/libSDL3_ttf.so* "$src/assets" "$LIB/"
ln -sf "$LIB/mdpad" "$BIN/mdpad"

# desktop entry, MIME glob, and icons -> default opener for Markdown files
cp -a "$src/share/." "$DATA/"
update-mime-database    "$DATA/mime"           >/dev/null 2>&1 || true
update-desktop-database "$DATA/applications"   >/dev/null 2>&1 || true
gtk-update-icon-cache -q -t -f "$DATA/icons/hicolor" >/dev/null 2>&1 || true
xdg-mime default mdpad.desktop text/markdown   >/dev/null 2>&1 || true

say "installed: $BIN/mdpad"
case ":$PATH:" in
  *":$BIN:"*) ;;
  *) say "note: add $BIN to your PATH to run 'mdpad' from anywhere" ;;
esac
say "done. open a file with:  mdpad file.md"
