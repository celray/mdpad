# Installation

The Linux builds bundle SDL3 and the fonts, so they run on most distributions
with no extra packages. Pick whichever option suits your system. All downloads
live on the [releases page](https://github.com/celray/mdpad/releases/latest).

## Downloads

| Platform | x86_64 / amd64 | ARM64 |
|----------|----------------|-------|
| Linux (Debian/Ubuntu) | `mdpad_1.0_amd64.deb` | _coming soon_ |
| Linux (portable) | `mdpad-1.0-linux-x86_64.tar.gz` | _coming soon_ |
| Windows / macOS | build from source | build from source |

## Debian / Ubuntu (.deb)

```bash
sudo apt install ./mdpad_1.0_amd64.deb
```

This installs the application to `/opt/mdpad` with a `mdpad` command on your
`PATH` and a desktop entry, so it also shows up in your application menu. To
remove it:

```bash
sudo apt remove mdpad
```

## Portable tarball (any distribution)

```bash
tar xzf mdpad-1.0-linux-x86_64.tar.gz
cd mdpad-1.0-linux-x86_64
./mdpad
```

Everything mdpad needs (the SDL libraries and the fonts) sits in that folder.
Keep the files together; the binary loads them from its own directory.

### Optional: install for your user, no root

The tarball includes an `install.sh` that copies mdpad into `~/.local` and adds
a menu entry, without touching system directories:

```bash
./install.sh
```

Make sure `~/.local/bin` is on your `PATH`, then run `mdpad` from anywhere.

## ARM64 Linux

ARM64 packages are on the way. Until then, [build from
source](building.md) on the target machine.

## Windows and macOS

There are no prebuilt binaries yet. The project builds on both from source with
CMake. See [Building from source](building.md).
