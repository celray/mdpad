# HTML rendering in mdpad

mdpad now renders embedded HTML instead of showing the raw tags. This file
mixes Markdown and HTML.

<div align="center">
  <h2>Centered block</h2>
  <p>This whole <code>&lt;div align="center"&gt;</code> is centered, including
  the <strong>heading</strong> above and this paragraph.</p>
</div>

## Inline HTML

Keyboard hints render with <kbd>Ctrl</kbd> + <kbd>K</kbd>, you can
<u>underline</u>, <s>strike</s>, <mark>highlight</mark>, and set a
<span style="color:#e11d48">custom colour</span> or a
<font color="green">named one</font>. Line breaks work too:<br>
this sits on the next line.

A real <a href="https://github.com/celray/mdpad">hyperlink</a> is underlined and
coloured.

## Block-level HTML

<blockquote>
  A blockquote written as HTML, with <em>emphasis</em> and a
  <a href="https://example.com">link</a> inside it.
</blockquote>

<table>
  <thead>
    <tr><th>Tag</th><th>Effect</th></tr>
  </thead>
  <tbody>
    <tr><td><code>&lt;b&gt;</code></td><td><b>bold</b></td></tr>
    <tr><td><code>&lt;i&gt;</code></td><td><i>italic</i></td></tr>
    <tr><td><code>&lt;mark&gt;</code></td><td><mark>marked</mark></td></tr>
  </tbody>
</table>

<details>
  <summary>Collapsible section (summary shown in bold)</summary>
  <p>The summary renders as a bold line and the inner content follows as
  normal paragraphs.</p>
</details>

## Lists and code as HTML

<ol>
  <li>First item</li>
  <li>Second item with <b>bold</b></li>
</ol>

<pre><code class="language-python">def greet(name):
    print(f"Hello, {name}!")
</code></pre>

## Entities

Ampersands &amp; angle brackets &lt;like this&gt;, em dashes &mdash; arrows
&rarr;, and symbols &copy; &reg; &trade; all decode.
