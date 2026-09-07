#!/usr/bin/env python3
"""Merges the vendored base token colors (Default Light+, snapshot) with the
Trust-specific theme overrides into a single self-contained color theme.

The Trust theme no longer relies on `"extends"` (which was not being honoured in
some environments, leaving C++ / other languages monochrome). Instead the base
token colors are baked in at build time, so the packaged theme always carries the
Light+ base for non-trust scopes plus the Trust overrides for `.trust` scopes.

Usage: merge_theme.py <base_theme.json> <trust_overrides.json> <out_theme.json>
- base_theme.json      : vendored snapshot ({"tokenColors": [...]} or plain array)
- trust_overrides.json : Trust-only overrides ({name,type,colors,tokenColors})
- out_theme.json       : self-contained merged theme to write
"""
import json
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    base_path, trust_path, out_path = sys.argv[1], sys.argv[2], sys.argv[3]

    with open(base_path, encoding="utf-8") as f:
        base = json.load(f)
    with open(trust_path, encoding="utf-8") as f:
        trust = json.load(f)

    base_tokens = base.get("tokenColors", base) if isinstance(base, dict) else base
    if not isinstance(base_tokens, list):
        print(f"error: base tokenColors must be a list in {base_path}", file=sys.stderr)
        return 2

    merged = {
        "name": trust.get("name", "Trust Language"),
        "type": trust.get("type", "light"),
        "colors": trust.get("colors", {}),
        "tokenColors": list(base_tokens) + list(trust.get("tokenColors", [])),
    }
    if "semanticTokenColors" in trust:
        merged["semanticTokenColors"] = trust["semanticTokenColors"]
    if "semanticHighlighting" in trust:
        merged["semanticHighlighting"] = trust["semanticHighlighting"]

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(merged, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
