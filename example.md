# Welcome to mdpad

A lightweight markdown viewer built with SDL3.

## Features

- **Fast** native rendering with SDL3 and SDL_ttf
- *Smooth* scrolling through long documents
- Cross-platform: Linux, macOS, Windows
- Open files with **Ctrl+O** or pass them as arguments

## Getting Started

To view a markdown file, simply run:

```
mdpad example.md
```

Or launch mdpad and press **Ctrl+O** to open a file dialog.

## Markdown Support

### Text Formatting

This viewer supports **bold text**, *italic text*, and `inline code`. You can also combine **bold and *italic*** styles within a paragraph for emphasis.

### Lists

- First item in the list
- Second item with more detail
- Third item to round things out

### Blockquotes

> "The best way to predict the future is to invent it."
> — Alan Kay

### Code Blocks

```
fn main() {
    println!("Hello from mdpad!");
    let items = vec![1, 2, 3];
    for item in items {
        println!("  item: {}", item);
    }
}
```

---

### Headings at Every Level

# Heading 1
## Heading 2
### Heading 3
#### Heading 4
##### Heading 5
###### Heading 6

---

## About

mdpad is a minimal markdown viewer that focuses on readability and speed. It parses markdown with md4c and renders text with SDL3_ttf using the Noto Sans font family.

Press **Escape** to quit. Happy reading!
