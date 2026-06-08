# Building from source

mdpad builds with CMake. All dependencies (SDL3, SDL_ttf, and md4c) are fetched
and built automatically through CMake's `FetchContent`, so there is nothing to
vendor or install by hand beyond a compiler and the system video headers.

## Requirements

- CMake 3.24 or newer
- A C++17 compiler (GCC 9+, Clang 10+, or MSVC 2019+)
- Git (CMake uses it to fetch the dependencies)

### Linux system packages

SDL3 needs the system video backend headers:

```bash
# Debian / Ubuntu
sudo apt install libx11-dev libxext-dev libwayland-dev libxkbcommon-dev

# Fedora
sudo dnf install libX11-devel libXext-devel wayland-devel libxkbcommon-devel
```

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/mdpad
```

The first build takes a few minutes because CMake downloads and compiles SDL3,
SDL_ttf (with FreeType and HarfBuzz), and md4c. Later builds are fast.

The compiled binary, the bundled SDL libraries, and the `assets/` folder all end
up in `build/bin/`. mdpad loads its fonts relative to the executable, so run it
from there or keep those files together when you move them.

## Platform notes

- **Windows**: Visual Studio 2019+ or MinGW with CMake.
- **macOS**: the Xcode command-line tools.
- **Linux**: GCC 9+ or Clang 10+, plus the video headers listed above.

## Building other architectures

Prebuilt x86_64 and ARM64 (aarch64) Linux packages ship with every release, so
most people can skip this (see [Installation](installation.md)). The same build
also runs on any other architecture: run it on the target machine (or a matching
toolchain) to produce native binaries. `packaging/build-linux.sh` reads
`uname -m`, so running it on an ARM64 host produces the aarch64 tarball and the
arm64 `.deb`. The release packages are assembled from the contents of
`build/bin/`.
