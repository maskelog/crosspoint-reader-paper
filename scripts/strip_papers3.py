#!/usr/bin/env python3
"""Strip PaperS3 conditional code from source files.

Handles these patterns (line-based, preserves indentation):

  Pattern A — keep M5Paper, drop PaperS3:
      #ifdef CROSSPOINT_PAPERS3
        ...drop...
      #elif defined(CROSSPOINT_M5PAPER)
        ...keep (unindent)...
      #endif

  Pattern B — drop entirely (PaperS3-only block, no M5Paper branch):
      #if CROSSPOINT_PAPERS3
        ...drop...
      #endif

  Pattern C — keep M5Paper, drop X3/X4 (#else):
      #if CROSSPOINT_PAPERS3 || CROSSPOINT_M5PAPER
        ...keep...
      #else
        ...drop...
      #endif

  Pattern D — keep M5Paper, drop X3/X4 (legacy PaperS3-only guard now used by M5Paper too):
      #if CROSSPOINT_PAPERS3
        ...keep...   (this branch was activated by manual #if expansion;
                      we treat it as the canonical M5Paper path)
      #else
        ...drop...
      #endif

  Pattern E — negated:
      #if !CROSSPOINT_PAPERS3
        ...drop...
      #endif

This is destructive — run on a clean working tree.
"""

import argparse
import re
import sys
from pathlib import Path

PAPERS3_GUARDS = (
    "CROSSPOINT_PAPERS3 || CROSSPOINT_M5PAPER",
    "CROSSPOINT_M5PAPER || CROSSPOINT_PAPERS3",
    "CROSSPOINT_PAPERS3",
)
M5PAPER_GUARDS = (
    "CROSSPOINT_M5PAPER",
)

IF_RE = re.compile(r"^\s*#\s*if(?:def)?\s+(.+?)\s*$")
ELIF_RE = re.compile(r"^\s*#\s*elif(?:\s+defined\s*\()?\s*(.+?)(?:\))?\s*$")
ELSE_RE = re.compile(r"^\s*#\s*else\b.*$")
ENDIF_RE = re.compile(r"^\s*#\s*endif\b.*$")

def is_papers3_guard(expr: str) -> bool:
    e = expr.strip().lstrip("(").rstrip(")").strip()
    e = e.replace("defined(", "").replace(")", "")
    e = " ".join(e.split())
    if e.startswith("!"):
        return False
    return e in PAPERS3_GUARDS or e == "CROSSPOINT_PAPERS3"

def is_negated_papers3(expr: str) -> bool:
    e = expr.strip()
    return e.startswith("!CROSSPOINT_PAPERS3") or e.startswith("! CROSSPOINT_PAPERS3")

def is_m5paper_guard(expr: str) -> bool:
    e = expr.strip().lstrip("(").rstrip(")").strip()
    e = e.replace("defined(", "").replace(")", "")
    e = " ".join(e.split())
    return e in M5PAPER_GUARDS or e in PAPERS3_GUARDS

def find_matching_end(lines, start_idx):
    """Return (else_idx_or_None, elif_indices_list, endif_idx)."""
    depth = 0
    else_idx = None
    elif_indices = []
    for i in range(start_idx + 1, len(lines)):
        line = lines[i]
        if IF_RE.match(line):
            depth += 1
        elif ENDIF_RE.match(line):
            if depth == 0:
                return else_idx, elif_indices, i
            depth -= 1
        elif depth == 0:
            if ELIF_RE.match(line):
                elif_indices.append(i)
            elif ELSE_RE.match(line):
                else_idx = i
    raise SyntaxError(f"Unmatched #if at line {start_idx + 1}")

def strip_file(path: Path, dry_run: bool = False) -> int:
    text = path.read_text(encoding="utf-8")
    lines = text.split("\n")
    out = []
    i = 0
    changes = 0
    while i < len(lines):
        line = lines[i]
        m = IF_RE.match(line)
        if not m:
            out.append(line)
            i += 1
            continue
        guard = m.group(1).strip()

        # Find structure of this conditional
        try:
            else_idx, elif_indices, end_idx = find_matching_end(lines, i)
        except SyntaxError as e:
            print(f"WARN {path}: {e}", file=sys.stderr)
            out.append(line)
            i += 1
            continue

        # Pattern A: PaperS3 ifdef + M5Paper elif
        if (
            guard.replace("defined(", "").replace(")", "").strip() == "CROSSPOINT_PAPERS3"
            and elif_indices
            and is_m5paper_guard(ELIF_RE.match(lines[elif_indices[0]]).group(1))
            and len(elif_indices) == 1
        ):
            # Keep elif branch as the only path
            elif_start = elif_indices[0] + 1
            elif_end = else_idx if else_idx is not None else end_idx
            kept = lines[elif_start:elif_end]
            out.extend(kept)
            changes += 1
            i = end_idx + 1
            continue

        # Pattern C/D: PaperS3 guard (with or without || M5Paper) — keep block, drop #else
        if is_papers3_guard(guard):
            inner_end = else_idx if else_idx is not None else end_idx
            kept = lines[i + 1:inner_end]
            out.extend(kept)
            changes += 1
            i = end_idx + 1
            continue

        # Pattern E: !PaperS3 — drop block, keep #else if present
        if is_negated_papers3(guard):
            if else_idx is not None:
                kept = lines[else_idx + 1:end_idx]
                out.extend(kept)
            changes += 1
            i = end_idx + 1
            continue

        # Pattern F: M5Paper guard with #else — keep M5Paper block, drop else
        if is_m5paper_guard(guard):
            inner_end = else_idx if else_idx is not None else end_idx
            # If there's no #else, this is just a redundant guard — keep contents only
            kept = lines[i + 1:inner_end]
            out.extend(kept)
            changes += 1
            i = end_idx + 1
            continue

        # Unknown — leave alone
        out.append(line)
        i += 1

    new_text = "\n".join(out)
    if new_text != text:
        if dry_run:
            print(f"  would change {path} ({changes} blocks)")
        else:
            path.write_text(new_text, encoding="utf-8", newline="\n")
            print(f"  changed {path} ({changes} blocks)")
    return changes

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", help="Files or directories")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    total = 0
    for p in args.paths:
        path = Path(p)
        if path.is_dir():
            for f in path.rglob("*"):
                if f.is_file() and f.suffix in {".cpp", ".h", ".c", ".hpp"}:
                    total += strip_file(f, args.dry_run)
        else:
            total += strip_file(path, args.dry_run)
    print(f"\nTotal: {total} blocks changed")

if __name__ == "__main__":
    main()
