<p align="center">
  <img src="assets/mdpad.svg" width="120" alt="mdpad logo">
</p>

<h1 align="center">mdpad</h1>

A lightweight, cross-platform markdown viewer with infinite scroll rendering.
Renders embedded HTML (headings, lists, tables, links, images, `<kbd>`,
`<mark>`, colours, centred blocks) rather than showing the raw tags.

Built with SDL3, SDL_ttf, and md4c.

Documentation: https://celray.github.io/mdpad/

## Install (Linux)

One line, no root. Downloads the latest release for your architecture and
installs it into `~/.local`:

```bash
curl -fsSL https://raw.githubusercontent.com/celray/mdpad/master/install.sh | sh
```

Once installed, `mdpad --update` pulls the latest release in place (and
`mdpad --check-update` just reports whether one exists). `MDPAD_PREFIX` changes
the install location and `MDPAD_VERSION` pins a release. There are also a
`.deb` and a portable tarball on the
[releases page](https://github.com/celray/mdpad/releases/latest); see the
[installation guide](https://celray.github.io/mdpad/installation/) for all the
options.

## Building

Requirements:
- CMake 3.24+
- C++17 compiler (GCC 9+, Clang 10+, MSVC 2019+)
- Git (for FetchContent dependency downloads)

```bash
git clone <repo-url> mdpad
cd mdpad
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/mdpad
```

All dependencies (SDL3, SDL3_ttf, md4c) are downloaded automatically on first build.

### Linux Dependencies

SDL3 requires system video backend headers:

```bash
# Debian/Ubuntu
sudo apt install libx11-dev libxext-dev libwayland-dev libxkbcommon-dev

# Fedora
sudo dnf install libX11-devel libXext-devel wayland-devel libxkbcommon-devel
```

## License

TBD
