#!/usr/bin/env python3
"""gen_tokens.py - Token generator.

Generates files in output-dir:
  parser.gen.y   - %token / %nterm for Bison
  token.gen.*    - TK/XE/XS/XD/XR data for C++ X-macros

Usage:
    python3 gen_tokens.py \\
        --input tokens.def \\
        --lock tokens.lock \\
        --output-dir build/generated \\
        --parser-template parser.y \\
        --lexer lexer.l
"""

import argparse
import hashlib
import os
import re
import sys
from pathlib import Path

# Bitmask constants (must match token.hpp)
FLEX_LEXEME  = 1
BISON_TOKEN  = 2
Expr         = 4
Stmt         = 8
Decl         = 16
Root         = 32

_AUTO_START = 256

_TOKEN_RE = re.compile(
    r'^TOKEN\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(AUTO|[0-9]+)\s*,\s*([^)]+)\s*\)\s*$'
)
_LOCK_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)=([0-9]+)$')
_UPPER_RE = re.compile(r'^[A-Z][A-Z0-9_]*$')
_LOWER_RE = re.compile(r'^[a-z][a-z0-9_]*$')
_CAMEL_RE = re.compile(r'^[A-Z][A-Za-z0-9]*[a-z][A-Za-z0-9]*$')


def compute_hash(path: str) -> str:
    """Hash only TOKEN lines (comments stripped)."""
    with open(path, 'r') as f:
        raw = f.read()
    raw = raw.replace('\r\n', '\n').replace('\r', '\n')
    raw = re.sub(r'//[^\n]*', '', raw)
    raw = re.sub(r'/\*.*?\*/', '', raw, flags=re.DOTALL)
    lines = [l.strip() for l in raw.split('\n') if l.strip() and l.strip().startswith('TOKEN')]
    content = '\n'.join(lines)
    return hashlib.md5(content.encode()).hexdigest()


def atomic_write(path: str, content: str) -> None:
    tmp = path + '.tmp'
    with open(tmp, 'w') as f:
        f.write(content)
    os.replace(tmp, path)


def die(msg: str) -> None:
    print(f"[TokenGen] FATAL: {msg}", file=sys.stderr)
    sys.exit(1)


def warn(msg: str) -> None:
    print(f"[TokenGen] WARNING: {msg}", file=sys.stderr)


def mask_to_cpp_flags(mask: int) -> str:
    """Convert bitmask to TokenFlag::* expression."""
    parts = []
    if mask & FLEX_LEXEME:
        parts.append("TokenFlag::FLEX_LEXEME")
    if mask & BISON_TOKEN:
        parts.append("TokenFlag::BISON_TOKEN")
    if mask & Expr:
        parts.append("TokenFlag::Expr")
    if mask & Stmt:
        parts.append("TokenFlag::Stmt")
    if mask & Decl:
        parts.append("TokenFlag::Decl")
    if mask & Root:
        parts.append("TokenFlag::Root")
    return " | ".join(parts) if parts else "TokenFlag{}"


def load_lock(path: str) -> dict:
    locked = {}
    if os.path.exists(path):
        with open(path, 'r') as f:
            for line in f:
                line = line.strip().lstrip(';')
                if not line or line.startswith('#'):
                    continue
                m = _LOCK_RE.match(line)
                if m:
                    locked[m.group(1)] = int(m.group(2))
                else:
                    warn(f"Bad lock file line: '{line}'")
    return locked


def parse_tokens_def(path: str, locked: dict):
    with open(path, 'r') as f:
        raw = f.read()

    raw = raw.replace('\r\n', '\n').replace('\r', '\n')
    raw = re.sub(r'//[^\n]*', '', raw)
    lines = [line.strip() for line in raw.split('\n') if line.strip()]

    explicit_vals = set()
    for line in lines:
        m = _TOKEN_RE.match(line)
        if m and m.group(2) != 'AUTO':
            explicit_vals.add(int(m.group(2)))

    all_taken = set(explicit_vals) | set(locked.values())
    next_auto = _AUTO_START
    tokens = []
    new_lock = {}

    for line in lines:
        m = _TOKEN_RE.match(line)
        if not m:
            continue

        name = m.group(1)
        val_str = m.group(2)
        flags_str = m.group(3).strip()

        if any(t['name'] == name for t in tokens):
            die(f"Token '{name}' defined twice in {path}")

        if val_str == 'AUTO':
            if name in locked:
                val = locked[name]
            else:
                while next_auto in all_taken:
                    next_auto += 1
                val = next_auto
                all_taken.add(val)
                new_lock[name] = val
                print(f"[TokenGen] New token: {name} = {val}")
                next_auto += 1
        else:
            val = int(val_str)
            if name in locked and locked[name] != val:
                die(
                    f"Token '{name}' value changed!\n"
                    f"  In tokens.def:  {val}\n"
                    f"  In tokens.lock: {locked[name]}\n"
                    f"  If intentional - update tokens.lock and rebuild."
                )
            if name not in locked:
                new_lock[name] = val

        mask = 0
        flag_map = {
            'FLEX_LEXEME': FLEX_LEXEME,
            'BISON_TOKEN': BISON_TOKEN,
            'Expr':        Expr,
            'Stmt':        Stmt,
            'Decl':        Decl,
            'Root':        Root,
        }
        for flag_name, bit in flag_map.items():
            if re.search(r'\b' + flag_name + r'\b', flags_str):
                mask |= bit

        tokens.append({
            'name': name,
            'value': val,
            'mask': mask,
        })

    return tokens, new_lock


def validate_tokens(tokens: list, locked: dict, lock_path: str) -> None:
    if not tokens:
        die("No TOKEN(...) found")

    seen = {}
    for t in tokens:
        v = t['value']
        if v in seen:
            die(f"Value collision: {t['name']} = {v} and {seen[v]} = {v}")
        seen[v] = t['name']

    end_tokens = [t for t in tokens if t['name'] == 'END']
    if not end_tokens:
        die("Token END not found.")
    if end_tokens[0]['value'] != 0:
        die(f"Token END must be 0, got: {end_tokens[0]['value']}")

    root_tokens = [t for t in tokens if t['mask'] & Root]
    if len(root_tokens) != 1:
        names = ', '.join(t['name'] for t in root_tokens)
        die(f"Expected exactly one Root token, found: {len(root_tokens)} ({names})")

    for t in tokens:
        name = t['name']
        mask = t['mask']
        has_flex = mask & FLEX_LEXEME
        has_expr = mask & Expr
        has_stmt = mask & Stmt
        has_decl = mask & Decl
        has_root = mask & Root
        has_bison = mask & BISON_TOKEN
        ast_count = sum(1 for x in (has_expr, has_stmt, has_decl, has_root) if x)

        if mask == 0:
            die(f"Token '{name}' has no flags.")

        if has_flex and ast_count:
            die(f"Incompatible flags in '{name}': FLEX_LEXEME + AST flag.")

        if has_bison and ast_count > 1:
            die(f"Too many AST flags in '{name}'.")

        is_upper = bool(_UPPER_RE.match(name))
        is_lower = bool(_LOWER_RE.match(name))
        is_camel = bool(_CAMEL_RE.match(name))
        is_ast = bool(has_expr or has_stmt or has_decl or has_root)
        only_bison = bool(has_bison and not has_flex and not is_ast)

        if is_upper and not has_flex:
            die(f"'{name}': UPPER_CASE requires FLEX_LEXEME.")
        if is_camel and not is_ast:
            die(f"'{name}': CamelCase requires AST flag.")
        if is_lower and not only_bison:
            die(f"'{name}': lower_case only for BISON_TOKEN.")
        if has_flex and not is_upper:
            die(f"'{name}': FLEX_LEXEME requires UPPER_CASE.")
        if only_bison and not is_lower:
            die(f"'{name}': sole BISON_TOKEN must be lower_case.")
        if is_ast and not is_camel:
            die(f"'{name}': AST node must be CamelCase.")

    token_names = {t['name'] for t in tokens}
    for lname in locked:
        if lname not in token_names:
            warn(f"Token '{lname}' removed from tokens.def but still in lock.")


_TK_RE = re.compile(r'\bTK\w*\(([A-Za-z_][A-Za-z0-9_]*)')
_LEXER_KIND_RE = re.compile(r'\bLexemeKind::([A-Za-z_][A-Za-z0-9_]*)\b')
_LEXER_ONLY_TOKENS = frozenset({'END'})


def _extract_lexer_tokens(lexer_path: str) -> set:
    found = set()
    if not os.path.exists(lexer_path):
        return found
    with open(lexer_path, 'r') as f:
        for line in f:
            if '#define TK' in line:
                continue
            for m in _TK_RE.finditer(line):
                found.add(m.group(1))
            for m in _LEXER_KIND_RE.finditer(line):
                found.add(m.group(1))
    return found


def cross_validate(tokens: list, lexer_path: str) -> None:
    if not os.path.exists(lexer_path):
        warn(f"lexer.l not found: {lexer_path} (skipping)")
        return

    lexer_tokens = _extract_lexer_tokens(lexer_path)
    flex_tokens = {t['name'] for t in tokens if t['mask'] & FLEX_LEXEME}

    missing_in_def = lexer_tokens - flex_tokens - _LEXER_ONLY_TOKENS
    if missing_in_def:
        die(f"TK() in lexer.l but missing FLEX_LEXEME: {', '.join(sorted(missing_in_def))}")

    missing_in_lexer = flex_tokens - lexer_tokens - _LEXER_ONLY_TOKENS
    if missing_in_lexer:
        die(f"Flex tokens in tokens.def but no TK() in lexer.l: {', '.join(sorted(missing_in_lexer))}")




def generate_parser_y(tokens: list, parser_template: str, tokens_hash: str) -> str:
    decls = []
    for t in tokens:
        if t['mask'] & BISON_TOKEN:
            if t['mask'] & FLEX_LEXEME:
                decls.append(f"%token {t['name']} {t['value']}")
            else:
                decls.append(f"%nterm {t['name']}")

    bison_decls = '\n'.join(decls) + '\n' if decls else ''
    header_comment = f"/* Generated by gen_tokens.py (hash: {tokens_hash}) */"

    src = parser_template
    src = src.replace('@@HEADER@@', header_comment)
    src = src.replace('@@TOKENS@@', bison_decls)
    return src


def generate_gen_files(tokens: list, tokens_hash: str) -> dict:
    """Generate .gen.* data files for X-macro inclusion.

    Returns dict: filename -> content
    """
    result = {}

    # Hash for sync check
    result["token.gen.hash"] = (
        "/* GENERATED - tokens.def hash */\n"
        f'#define TOKENS_DEF_HASH "{tokens_hash}"\n'
    )

    # All tokens: TK(name, val, flags)
    lines = ["/* GENERATED: all tokens - TK(name, val, flags) */"]
    for t in tokens:
        flags = mask_to_cpp_flags(t['mask'])
        lines.append(f"TK({t['name']}, {t['value']}, {flags})")
    result["token.gen.all"] = '\n'.join(lines) + '\n'

    # name_all: TCASE(name) for name() switch
    lines = ["/* GENERATED: TCASE(name) for ParserToken::name() */"]
    for t in tokens:
        lines.append(f"TCASE({t['name']})")
    result["token.gen.name_all"] = '\n'.join(lines) + '\n'

    # flags_all: FCASE(name, flags) for flags() switch
    lines = ["/* GENERATED: FCASE(name, flags) for ParserToken::flags() */"]
    for t in tokens:
        flags = mask_to_cpp_flags(t['mask'])
        lines.append(f"FCASE({t['name']}, {flags})")
    result["token.gen.flags_all"] = '\n'.join(lines) + '\n'

    # lookup_all: LOOKUP(name) for from_name()
    lines = ["/* GENERATED: LOOKUP(name) for ParserToken::from_name() */"]
    for t in tokens:
        lines.append(f"LOOKUP({t['name']})")
    result["token.gen.lookup_all"] = '\n'.join(lines) + '\n'

    return result


def save_lock(path: str, locked: dict, new_lock: dict) -> None:
    lines = [
        "# tokens.lock - fixed token values. COMMIT TO VCS.",
    ]
    for name, val in locked.items():
        lines.append(f"{name}={val}")
    for name, val in new_lock.items():
        if name not in locked:
            lines.append(f"{name}={val}")
    atomic_write(path, '\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(description='Token generator')
    parser.add_argument('--input', required=True, help='Path to tokens.def')
    parser.add_argument('--lock', required=True, help='Path to lock file')
    parser.add_argument('--output-dir', required=True, help='Output directory')
    parser.add_argument('--parser-template', required=True, help='Path to parser.y template')
    parser.add_argument('--lexer', required=False, help='Path to lexer.l (cross-validation)')
    args = parser.parse_args()

    for p in [args.input, args.parser_template]:
        if not os.path.exists(p):
            die(f"File not found: {p}")

    locked = load_lock(args.lock)
    print(f"[TokenGen] Lock file: {args.lock} ({len(locked)} entries)")

    tokens, new_lock = parse_tokens_def(args.input, locked)

    if new_lock:
        save_lock(args.lock, locked, new_lock)
        print(f"[TokenGen] tokens.lock updated (+{len(new_lock)} tokens).")

    validate_tokens(tokens, locked, args.lock)

    if args.lexer:
        cross_validate(tokens, args.lexer)
        print(f"[TokenGen] Cross-validation lexer.l passed")

    tokens_hash = compute_hash(args.input)

    os.makedirs(args.output_dir, exist_ok=True)

    flex_count = sum(1 for t in tokens if t['mask'] & FLEX_LEXEME)

    with open(args.parser_template, 'r') as f:
        parser_src = f.read()
    parser_y = generate_parser_y(tokens, parser_src, tokens_hash)
    token_count = sum(1 for t in tokens if (t['mask'] & BISON_TOKEN) and (t['mask'] & FLEX_LEXEME))
    nterm_count = sum(1 for t in tokens if (t['mask'] & BISON_TOKEN) and not (t['mask'] & FLEX_LEXEME))
    atomic_write(os.path.join(args.output_dir, 'parser.gen.y'), parser_y)
    print(f"[TokenGen] Written: parser.gen.y ({token_count} %token, {nterm_count} %nterm)")

    # Generate .gen.* files
    gen_files = generate_gen_files(tokens, tokens_hash)
    for fname, content in gen_files.items():
        atomic_write(os.path.join(args.output_dir, fname), content)
        print(f"[TokenGen] Written: {fname}")

    # Legacy token.gen.def (backward compatibility)
    legacy_lines = []
    legacy_lines.append(f'#define TOKENS_DEF_HASH "{tokens_hash}"')
    legacy_lines.append("")
    legacy_lines.append("/* All tokens: TK(name, value, cpp_flags) */")
    for t in tokens:
        flags = mask_to_cpp_flags(t['mask'])
        legacy_lines.append(f"TK({t['name']}, {t['value']}, {flags})")
    legacy_lines.append("")
    ast_cats = [
        ("Expr", Expr, "XE"),
        ("Stmt", Stmt, "XS"),
        ("Decl", Decl, "XD"),
        ("Root", Root, "XR"),
    ]
    for cat_name, cat_mask, macro in ast_cats:
        legacy_lines.append(f"/* AST {cat_name} nodes: {macro}(name, value) */")
        for t in tokens:
            if t['mask'] & cat_mask:
                legacy_lines.append(f"{macro}({t['name']}, {t['value']})")
        legacy_lines.append("")
    atomic_write(os.path.join(args.output_dir, 'token.gen.def'), '\n'.join(legacy_lines) + '\n')

    token_count_all = len(tokens)
    expr_count = sum(1 for t in tokens if t['mask'] & Expr)
    stmt_count = sum(1 for t in tokens if t['mask'] & Stmt)
    decl_count = sum(1 for t in tokens if t['mask'] & Decl)
    root_count = sum(1 for t in tokens if t['mask'] & Root)

    print(f"[TokenGen] Written: token.gen.def ({token_count_all} tokens, legacy)")
    print(f"[TokenGen]   FLEX_LEXEME  : {flex_count} (via ParserToken::Kind)")
    print(f"[TokenGen]   %token : {token_count}")
    print(f"[TokenGen]   %nterm : {nterm_count}")
    print(f"[TokenGen]   Expr   : {expr_count}")
    print(f"[TokenGen]   Stmt   : {stmt_count}")
    print(f"[TokenGen]   Decl   : {decl_count}")
    print(f"[TokenGen]   Root   : {root_count}")
    print(f"[TokenGen]   hash   : {tokens_hash}")


if __name__ == '__main__':
    main()