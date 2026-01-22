#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Hide all 'Description' fields in KiCad .kicad_sch files by inserting (hide) into
the (effects ...) block of (property "Description" ...).

- Makes a .bak backup before modifying.
- Safe-ish parser: finds S-expression ranges while respecting quoted strings.
"""

from __future__ import annotations

import argparse
import glob
import os
from dataclasses import dataclass

TARGET_PROP = '(property "Description"'

@dataclass
class EditResult:
    path: str
    changed: bool
    count: int

def _find_matching_paren(text: str, start_idx: int) -> int:
    """
    Given index at '(' returns index of matching ')' (inclusive).
    Ignores parentheses inside quoted strings.
    """
    assert text[start_idx] == '('
    depth = 0
    in_str = False
    esc = False

    for i in range(start_idx, len(text)):
        c = text[i]
        if in_str:
            if esc:
                esc = False
            elif c == '\\':
                esc = True
            elif c == '"':
                in_str = False
            continue
        else:
            if c == '"':
                in_str = True
                continue
            if c == '(':
                depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    return i
    raise ValueError("Unbalanced parentheses; cannot find matching ')'")

def _hide_description_in_text(text: str) -> tuple[str, int]:
    """
    Returns (new_text, modifications_count).
    Inserts (hide) into (effects ...) block for each Description property missing it.
    """
    i = 0
    mods = 0
    out = []
    while True:
        j = text.find(TARGET_PROP, i)
        if j < 0:
            out.append(text[i:])
            break

        # Copy up to the property
        out.append(text[i:j])

        # We are at '(property "Description"...'
        prop_start = j
        # Find the '(' that starts this property form (should be exactly at j)
        if text[prop_start] != '(':
            # Shouldn't happen, but fail safe: just copy and continue
            out.append(text[prop_start:prop_start + len(TARGET_PROP)])
            i = prop_start + len(TARGET_PROP)
            continue

        prop_end = _find_matching_paren(text, prop_start)
        prop_block = text[prop_start:prop_end + 1]

        # Locate (effects ... ) inside this property block
        eff_idx = prop_block.find('(effects')
        if eff_idx < 0:
            # No effects block: copy as-is
            out.append(prop_block)
            i = prop_end + 1
            continue

        # Find end of (effects ...) within the property block
        # We need absolute index in prop_block
        eff_abs_start = eff_idx
        eff_abs_end = _find_matching_paren(prop_block, eff_abs_start)
        effects_block = prop_block[eff_abs_start:eff_abs_end + 1]

        if '(hide)' in effects_block:
            # Already hidden
            out.append(prop_block)
            i = prop_end + 1
            continue

        # Insert '(hide)' right after '(effects'
        insert_pos = eff_abs_start + len('(effects')
        # Keep formatting minimal: add a space then (hide)
        new_prop_block = (
            prop_block[:insert_pos] +
            ' (hide)' +
            prop_block[insert_pos:]
        )
        out.append(new_prop_block)
        mods += 1
        i = prop_end + 1

    return ''.join(out), mods

def process_file(path: str, dry_run: bool = False) -> EditResult:
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()

    new_text, mods = _hide_description_in_text(text)

    if mods == 0:
        return EditResult(path=path, changed=False, count=0)

    if not dry_run:
        bak = path + ".bak"
        if not os.path.exists(bak):
            with open(bak, 'w', encoding='utf-8') as f:
                f.write(text)
        with open(path, 'w', encoding='utf-8') as f:
            f.write(new_text)

    return EditResult(path=path, changed=True, count=mods)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inputs", nargs="+",
                    help='One or more .kicad_sch files or glob patterns (e.g. "**/*.kicad_sch")')
    ap.add_argument("--dry-run", action="store_true", help="Do not write files; only report changes.")
    args = ap.parse_args()

    files: list[str] = []
    for p in args.inputs:
        expanded = glob.glob(p, recursive=True)
        if expanded:
            files.extend(expanded)
        else:
            files.append(p)

    files = [f for f in files if f.endswith(".kicad_sch") and os.path.isfile(f)]
    if not files:
        raise SystemExit("No .kicad_sch files found in inputs.")

    total_mods = 0
    changed_files = 0
    for fpath in sorted(set(files)):
        r = process_file(fpath, dry_run=args.dry_run)
        if r.changed:
            changed_files += 1
            total_mods += r.count
        status = "CHANGED" if r.changed else "OK"
        print(f"{status}: {r.path}  (updated {r.count} Description fields)")

    print(f"\nSummary: {changed_files}/{len(set(files))} files changed; {total_mods} fields updated.")
    if args.dry_run:
        print("Dry-run mode: no files were written.")

if __name__ == "__main__":
    main()
