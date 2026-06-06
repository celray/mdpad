# mdpad

A lightweight, cross-platform markdown viewer with infinite scroll rendering.

Built with SDL3, SDL_ttf, and md4c.

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
