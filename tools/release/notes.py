#!/usr/bin/env python3
"""Release notes from README.md: install instructions + the "Status" section + checksums.

usage: notes.py README.md VERSION SHA256SUMS.txt > notes.md
"""
import re
import sys


def section(text, title_prefix):
    m = re.search(r"^## %s.*?$\n(.*?)(?=^## )" % re.escape(title_prefix), text, re.S | re.M)
    return m.group(1).strip() if m else ""


def main():
    readme, version, sums = sys.argv[1:4]
    with open(readme, encoding="utf-8") as f:
        text = f.read()
    status = section(text, "Status")
    with open(sums) as f:
        checksums = f.read().strip()
    print("""**Twilight Princess HD, native PC port, %s**

This release contains **no game files, no game code and no keys**. You need your own disc dump
(.wux/.wud with its disc key, plus the Wii U common key from your console), a Cemu .wua archive
(no keys needed), or an already extracted game folder. The installer builds the game from it on your machine.

**Install** (a Mac with Apple Silicon, macOS 14 or newer): download the zip, unzip it anywhere and start
**Twilight Princess HD**. The first start prepares the game once from your dump (about two minutes); later
starts launch it directly. Everything stays in that folder. The app is not signed by Apple:
macOS 15+: System Settings > Privacy & Security > Open Anyway; macOS 14: right-click > Open.
Keep `Twilight Princess HD.app` inside the unzipped folder (move the whole folder, not just the app).

See "Install with the setup app" in the README for details.

## Status

%s

## Checksums (SHA-256)

```
%s
```
""" % (version, status or "See the README.", checksums))


if __name__ == "__main__":
    main()
