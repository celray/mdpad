# Second Document

This is a second markdown file to demonstrate **tab support**.

## How Tabs Work

- Open multiple files: `mdpad file1.md file2.md`
- **Ctrl+O** opens a new file in a new tab
- **Ctrl+W** closes the current tab
- **Ctrl+Tab** / **Ctrl+Shift+Tab** cycles through tabs
- **Middle-click** a tab to close it
- Click the **x** button on a tab to close it

> Tabs only appear when two or more documents are open.

---

## Some Content

Here is some filler text so you can scroll through this document independently of the first one. Each tab remembers its own scroll position, so switching back and forth preserves where you left off.

### A Code Example

```
fn fibonacci(n: u64) -> u64 {
    match n {
        0 => 0,
        1 => 1,
        _ => fibonacci(n - 1) + fibonacci(n - 2),
    }
}
```

That is all for now.
