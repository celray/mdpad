# Usage

## Opening files

Pass one or more files on the command line:

```bash
mdpad README.md
mdpad notes.md todo.md spec.md
```

Each file opens in its own tab. You can also open a file from inside mdpad with
``Ctrl+O``, which brings up your system file dialog (it filters for `.md`,
`.markdown`, and `.txt`).

### Single instance

If mdpad is already running, opening more files hands them to that window as new
tabs instead of starting a second process. This keeps everything in one place,
which is handy when Markdown files are wired to open with mdpad from your file
manager.

## Tabs

When more than one file is open, a tab bar appears at the top.

- **Click** a tab to switch to it.
- **Click the ×** on a tab, or **middle-click** the tab, to close it.
- **Ctrl+Tab** / **Ctrl+Shift+Tab** cycle forward and backward.
- **Ctrl+W** closes the active tab.

## Selecting and copying text

mdpad lets you select rendered text and copy it as plain text.

- **Click and drag** to select a range.
- **Double-click** to select a word.
- **Triple-click** to select a line.
- **Shift+click** to extend the current selection to where you click.
- **Ctrl+A** selects the whole document.
- **Ctrl+C** copies the selection to the clipboard.
- **Esc** clears the selection (and, if there is no selection, closes mdpad).

You can also move a caret through the text with the keyboard and hold **Shift**
to select as you go. See [Keybindings](keybindings.md) for the full list.

## Exporting and printing

Press **Ctrl+P** to export the active tab to HTML and open it in your default
browser, themed to match the on-screen view. From the browser you can print it
or save it as a PDF. The exported file is written to mdpad's per-user data
directory and overwritten on each export.

## Themes

mdpad renders in a light or dark theme, covering headings, code blocks, inline
code, block quotes, list markers, and syntax-highlighted code.
