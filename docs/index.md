# mdpad

A lightweight, cross-platform markdown viewer with infinite-scroll rendering.
Built with [SDL3](https://github.com/libsdl-org/SDL),
[SDL_ttf](https://github.com/libsdl-org/SDL_ttf), and
[md4c](https://github.com/mity/md4c).

mdpad opens a Markdown file and renders it as one continuous scrolling
document, the way a reader expects, without the chrome of an editor. It starts
fast, keeps memory low, and ships with its fonts and SDL libraries bundled so
the Linux builds run on most distributions without extra packages.

## Features

- **Infinite-scroll rendering** of the whole document, no pagination.
- **Tabs** for several files at once, with click, middle-click, and keyboard
  switching.
- **Text selection and copy** by character, word, line, or the whole document.
- **Caret navigation** with the arrow keys, word jumps, and document edges.
- **Syntax highlighting** inside fenced code blocks.
- **Embedded HTML** is rendered, not shown as raw tags: headings, paragraphs,
  lists, tables, blockquotes, `<pre>`, links, images, `<kbd>`, `<mark>`,
  colours, and centred `<div>` blocks.
- **Export to HTML** for printing, themed to match what you see on screen.
- **Self-update** with `mdpad --update`, pulling the latest release.
- **Single instance**: opening a file hands it to the running window as a new
  tab instead of starting a second process.
- **Light and dark** rendering themes.

## Quick start

=== "One line (Linux)"

    ```bash
    curl -fsSL https://raw.githubusercontent.com/celray/mdpad/master/install.sh | sh
    mdpad README.md
    ```

=== "Debian / Ubuntu"

    ```bash
    sudo apt install ./mdpad_1.1.1_amd64.deb
    mdpad README.md
    ```

=== "Portable tarball"

    ```bash
    tar xzf mdpad-1.1.1-linux-x86_64.tar.gz
    cd mdpad-1.1.1-linux-x86_64
    ./mdpad README.md
    ```

The one-line installer pulls the latest release and installs it into `~/.local`
with no root. Grab the downloads from the
[latest release](https://github.com/celray/mdpad/releases/latest), or read the
[installation guide](installation.md) for every option.

## Where to next

- [Installation](installation.md) — packages, the portable tarball, and a
  no-root install.
- [Usage](usage.md) — opening files, tabs, selection, and HTML export.
- [Keybindings](keybindings.md) — the full shortcut reference.
- [Building from source](building.md) — for other platforms and architectures.
