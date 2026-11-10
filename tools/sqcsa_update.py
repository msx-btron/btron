#!/usr/bin/env python3
"""
sqcsa_update.py — Security and Quality Code Static Analysis updater.

Usage:
    python3 tools/sqcsa_update.py <rel-folder> [<rel-folder> ...]

Each <rel-folder> is a path relative to the project root, e.g.:
    src/apps/clu/be
    src/apps/clu
    src/kernel
    src/fs
    src/drivers

For each folder the script runs:
    1. flawfinder --minlevel=4 --quiet <folder>/
    2. cppcheck --std=c99 --enable=warning,portability --quiet <folder>/
    3. clang --analyze -std=c99 (on individual .c files, skipping arch/ tables
       that exceed the interactive time budget — noted in the report)

It then rewrites the matching  "── sec-<name>  (<folder>/)"  block inside
doc/txt/SQCSA.txt, or appends a new block when none exists, and updates the
summary table row for that target.

Run from the project root:
    cd /Users/tonpa/depot/bitedits/btron
    python3 tools/sqcsa_update.py src/kernel src/fs src/drivers
"""

import argparse
import os
import re
import subprocess
import sys
from datetime import date
from pathlib import Path

# ─── configuration ────────────────────────────────────────────────────────────

ROOT = Path(__file__).resolve().parent.parent   # project root
SQCSA = ROOT / "doc" / "txt" / "SQCSA.txt"

# Extra include flags per top-level folder (additive on top of always-includes)
INCLUDE_EXTRAS: dict[str, list[str]] = {
    "src/apps/clu/be": ["-Isrc/apps/clu", "-Isrc/apps/clu/be"],
    "src/apps/clu":    ["-Isrc/apps/clu"],
    "src/kernel":      ["-Iinclude/drivers/broadcom"],
    "src/fs":          ["-Iinclude/drivers/broadcom"],
    "src/drivers":     ["-Iinclude/drivers", "-Iinclude/drivers/broadcom"],
}
ALWAYS_INCLUDES = ["-Iinclude", "-Iinclude/rt"]

# Folders whose arch/ sub-tree is too large for interactive clang --analyze
# (slice-8 work; flawfinder + cppcheck still cover them)
CLANG_SKIP_SUBDIRS: dict[str, list[str]] = {
    "src/apps/clu/be": ["arch"],
}

# Folders that skip clang --analyze entirely (driver headers don't parse
# cleanly under a POSIX host toolchain without a full cross-compile env)
CLANG_SKIP_ENTIRELY: set[str] = {"src/drivers"}

# ─── helpers ─────────────────────────────────────────────────────────────────

def run(cmd: list[str], cwd: Path | None = None) -> tuple[str, str, int]:
    """Run *cmd*, return (stdout, stderr, returncode)."""
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return r.stdout, r.stderr, r.returncode


def folder_key(folder: str) -> str:
    """'src/apps/clu/be'  →  'be'  (last path component)."""
    return Path(folder).name


def sec_target_name(folder: str) -> str:
    """Human-readable target name used in Makefile and SQCSA headings."""
    key = folder_key(folder)
    return f"sec-{key}"


# ─── runner: flawfinder ───────────────────────────────────────────────────────

def run_flawfinder(folder: str) -> dict:
    """
    Returns {
        lines_analyzed: int,
        hits_4plus: int,
        hits_5plus: int,
        findings: list[str],   # raw hit lines from --quiet output
        summary_line: str,
    }
    """
    path = str(ROOT / folder) + "/"
    stdout, stderr, _ = run(
        ["flawfinder", "--minlevel=4", "--quiet", path],
        cwd=ROOT,
    )
    # Also run without --quiet to get the stats footer
    stdout_full, _, _ = run(
        ["flawfinder", "--minlevel=4", path],
        cwd=ROOT,
    )

    findings: list[str] = []
    for line in stdout.splitlines():
        line = line.strip()
        if line and not line.startswith("No hits found") and not line.startswith("Hits"):
            findings.append(line)

    lines_analyzed = 0
    hits_4plus = 0
    hits_5plus = 0
    for line in stdout_full.splitlines():
        m = re.search(r"Lines analyzed\s*[:=]\s*([\d]+)", line)
        if m:
            lines_analyzed = int(m.group(1).replace(",", ""))
        m = re.search(r"Hits@level\+\s*=.*?\[4\+\]\s*([\d]+)", line)
        if m:
            hits_4plus = int(m.group(1))
        m = re.search(r"Hits@level\+\s*=.*?\[5\+\]\s*([\d]+)", line)
        if m:
            hits_5plus = int(m.group(1))

    summary = (
        f"Lines analyzed : {lines_analyzed:,}\n"
        f"    Hits@level 4   : {hits_4plus}   "
        f"({'zero at level 5' if hits_5plus == 0 else f'{hits_5plus} at level 5'})\n"
        f"    Hits@level 4+  : {hits_4plus}"
    )
    return dict(
        lines_analyzed=lines_analyzed,
        hits_4plus=hits_4plus,
        hits_5plus=hits_5plus,
        findings=findings,
        summary_line=summary,
    )


# ─── runner: cppcheck ────────────────────────────────────────────────────────

def run_cppcheck(folder: str) -> dict:
    """
    Returns {
        findings: list[str],   # diagnostic lines
        total: int,
    }
    """
    path = str(ROOT / folder) + "/"
    includes = ALWAYS_INCLUDES + INCLUDE_EXTRAS.get(folder, [])
    cmd = (
        ["cppcheck", "--std=c99", "--enable=warning,portability", "--quiet"]
        + includes
        + [path]
    )
    stdout, stderr, _ = run(cmd, cwd=ROOT)
    # cppcheck writes diagnostics to stderr
    raw = (stdout + stderr).strip()
    findings: list[str] = []
    for line in raw.splitlines():
        line = line.strip()
        if line and ": " in line and not line.startswith("Checking "):
            findings.append(line)
    return dict(findings=findings, total=len(findings))


# ─── runner: clang --analyze ─────────────────────────────────────────────────

def run_clang_analyze(folder: str) -> dict:
    """
    Returns {
        skipped: bool,
        skip_reason: str,
        warnings: list[str],
        total: int,
    }
    """
    if folder in CLANG_SKIP_ENTIRELY:
        return dict(
            skipped=True,
            skip_reason=(
                "driver headers pull in target-specific cpu_conf.h and broadcom MMIO "
                "registers that do not parse cleanly under a posix host toolchain"
            ),
            warnings=[],
            total=0,
        )

    skip_subdirs = CLANG_SKIP_SUBDIRS.get(folder, [])
    folder_path = ROOT / folder

    # Collect .c files
    c_files: list[Path] = []
    for f in sorted(folder_path.rglob("*.c")):
        rel = f.relative_to(folder_path)
        parts = rel.parts
        # Skip test helpers
        if any(p in ("test.c",) for p in [rel.name]):
            continue
        # Skip subdirs that are too large
        if skip_subdirs and parts[0] in skip_subdirs:
            continue
        c_files.append(f)

    if not c_files:
        return dict(skipped=False, skip_reason="", warnings=[], total=0)

    includes = ALWAYS_INCLUDES + INCLUDE_EXTRAS.get(folder, [])
    cmd = (
        ["clang", "--analyze", "-std=c99", "-Wall", "-Wextra"]
        + includes
        + ["-Xanalyzer", "-analyzer-output=text"]
        + [str(f) for f in c_files]
    )
    stdout, stderr, _ = run(cmd, cwd=ROOT)
    raw = stdout + stderr
    warnings: list[str] = []
    for line in raw.splitlines():
        # Keep only "warning:" lines (not notes, not errors we can't fix)
        if "warning:" in line or "error:" in line:
            # Strip absolute path prefix down to relative
            line = line.replace(str(ROOT) + "/", "")
            warnings.append(line.strip())

    skip_note = ""
    if skip_subdirs:
        skip_note = (
            f"clang --analyze is not run on "
            + ", ".join(f"{s}/" for s in skip_subdirs)
            + " during the gate; those subdirectories are slice-8 work."
        )

    return dict(
        skipped=False,
        skip_reason=skip_note,
        warnings=warnings,
        total=len(warnings),
    )


# ─── SQCSA.txt section builder ───────────────────────────────────────────────

def build_section(folder: str, fw: dict, cpp: dict, clang: dict) -> str:
    target = sec_target_name(folder)
    today = date.today().isoformat()

    lines: list[str] = []
    lines.append(f"  ── {target}  ({folder}/)  —  updated {today}  {'─' * max(0, 68 - len(folder) - len(today))}")
    lines.append("")

    # clang --analyze
    if clang["skipped"]:
        lines.append(f"  clang --analyze is not run on {folder}/ in this target because the")
        # wrap the reason
        reason = clang["skip_reason"]
        words = reason.split()
        row = "  "
        for w in words:
            if len(row) + len(w) + 1 > 76:
                lines.append(row)
                row = "  " + w
            else:
                row = (row + " " + w).lstrip()
                row = "  " + row.lstrip()
        if row.strip():
            lines.append(row)
        lines.append("  flawfinder and cppcheck cover the directory.")
    else:
        if clang["skip_reason"]:
            lines.append(f"  clang --analyze  {folder}/*.c (arch/ skipped — slice-8 work)")
        else:
            lines.append(f"  clang --analyze  {folder}/*.c")

        if clang["warnings"]:
            for w in clang["warnings"][:20]:   # cap at 20 for readability
                lines.append(f"    {w}")
            if len(clang["warnings"]) > 20:
                lines.append(f"    ... ({len(clang['warnings']) - 20} more)")
        else:
            lines.append("    (no diagnostics)")

    lines.append("")

    # flawfinder
    fw_path = folder + "/"
    lines.append(f"  flawfinder --minlevel=4  {fw_path}")
    lines.append(f"    Lines analyzed : {fw['lines_analyzed']:,}")
    lines.append(f"    Hits@level 4   : {fw['hits_4plus']}"
                 + (f"   (zero at level 5)" if fw['hits_5plus'] == 0 else f"   ({fw['hits_5plus']} at level 5)"))
    lines.append(f"    Hits@level 4+  : {fw['hits_4plus']}")
    if fw["hits_4plus"] == 0:
        lines.append(f"  Clean at level 4.  {folder}/ passes the flawfinder gate today.")
    elif folder in CLANG_SKIP_SUBDIRS and fw["findings"]:
        # Group by subdir if most hits are in arch/
        arch_hits = sum(1 for f in fw["findings"] if "/arch/" in f)
        if arch_hits == fw["hits_4plus"]:
            lines.append(f"  All {fw['hits_4plus']} hits are inside arch/ (slice-8 disassembler work).")
            lines.append("  Gate will trigger on level 5 or on any new level-4 hit outside arch/.")
        else:
            lines.append(f"  {arch_hits} of {fw['hits_4plus']} hits are inside arch/.")
    lines.append("")

    # cppcheck
    lines.append(f"  cppcheck  {folder}/")
    if cpp["findings"]:
        for f in cpp["findings"][:30]:
            # strip absolute paths
            f = f.replace(str(ROOT) + "/", "")
            lines.append(f"    {f}")
        if len(cpp["findings"]) > 30:
            lines.append(f"    ... ({len(cpp['findings']) - 30} more findings)")
    else:
        lines.append("    (no findings)")

    lines.append("")
    return "\n".join(lines)


# ─── summary table update ────────────────────────────────────────────────────

def gate_status(fw: dict, cpp: dict, clang: dict, folder: str) -> str:
    # PASS only when hits_4plus==0 AND cppcheck==0 AND clang==0
    if fw["hits_4plus"] == 0 and cpp["total"] == 0 and clang["total"] == 0:
        return "PASS"
    return "INFO"


def summary_row(folder: str, fw: dict, cpp: dict, clang: dict) -> str:
    target = sec_target_name(folder)
    fw_col = str(fw["hits_4plus"]) if fw["hits_4plus"] else "0"
    # note arch/ if most are there
    if folder in CLANG_SKIP_SUBDIRS and fw["hits_4plus"] > 0:
        fw_col += " (arch/)"
    cpp_col = str(cpp["total"]) if cpp["total"] else "0"
    clang_col = "n/a" if clang["skipped"] else str(clang["total"])
    gate = gate_status(fw, cpp, clang, folder)
    return (
        f"    {target:<12} {fw_col:>13}  {cpp_col:>17}  {clang_col:>14}  {gate}"
    )


# ─── SQCSA.txt patcher ───────────────────────────────────────────────────────

SECTION_RE = re.compile(
    r"(  ── sec-(?P<name>[a-z_-]+)\s+\([^)]+\)[^\n]*\n)"
    r"(?P<body>.*?)"
    r"(?=\n  ── (?:sec-|Summary)|\Z)",
    re.DOTALL,
)

SUMMARY_TABLE_RE = re.compile(
    r"(  ── Summary table [─]+\n\n"
    r"    Target[^\n]+\n"
    r"    [─]+\n)"
    r"(?P<rows>.*?)"
    r"(\n\n  Gate = PASS)",
    re.DOTALL,
)

SUMMARY_ROW_RE = re.compile(
    r"^    (?P<target>sec-[a-z_-]+)\s+.*$",
    re.MULTILINE,
)


def patch_sqcsa(folder: str, section_text: str,
                row_text: str, sqcsa_path: Path) -> None:
    content = sqcsa_path.read_text(encoding="utf-8")
    target = sec_target_name(folder)

    # --- replace or append section ---
    # Pattern: heading line + body up to next heading or end of section 10 block
    section_pattern = re.compile(
        r"  ── " + re.escape(target) + r"\s+\([^)]+\)[^\n]*\n"
        r".*?"
        r"(?=\n  ── (?:sec-|Summary)|\Z)",
        re.DOTALL,
    )
    if section_pattern.search(content):
        content = section_pattern.sub(section_text.rstrip("\n"), content, count=1)
    else:
        # Append before "── Summary table"
        summary_anchor = "  ── Summary table"
        if summary_anchor in content:
            content = content.replace(
                summary_anchor,
                section_text + "\n" + summary_anchor,
                1,
            )
        else:
            content = content.rstrip("\n") + "\n\n" + section_text

    # --- replace or append summary row ---
    row_pattern = re.compile(r"^    " + re.escape(target) + r"\s+.*$", re.MULTILINE)
    if row_pattern.search(content):
        content = row_pattern.sub(row_text, content, count=1)
    else:
        # Append a new row before the blank line after the last row
        end_rows_pattern = re.compile(
            r"(^    sec-[a-z_-]+\s+.*$\n)(\n  Gate = PASS)", re.MULTILINE
        )
        content = end_rows_pattern.sub(
            r"\g<1>" + row_text + r"\n\g<2>", content, count=1
        )

    sqcsa_path.write_text(content, encoding="utf-8")
    print(f"  SQCSA.txt patched for {target}")


# ─── main ────────────────────────────────────────────────────────────────────

def process_folder(folder: str, dry_run: bool = False) -> None:
    folder = folder.rstrip("/")
    folder_path = ROOT / folder
    if not folder_path.is_dir():
        print(f"[ERROR] folder not found: {folder_path}", file=sys.stderr)
        return

    target = sec_target_name(folder)
    print(f"\n{'=' * 60}")
    print(f"  {target}  ({folder}/)")
    print(f"{'=' * 60}")

    print("  → flawfinder ...", end=" ", flush=True)
    fw = run_flawfinder(folder)
    print(f"hits@4+ = {fw['hits_4plus']}")

    print("  → cppcheck  ...", end=" ", flush=True)
    cpp = run_cppcheck(folder)
    print(f"findings = {cpp['total']}")

    print("  → clang --analyze ...", end=" ", flush=True)
    clang = run_clang_analyze(folder)
    if clang["skipped"]:
        print("skipped (see report)")
    else:
        print(f"warnings = {clang['total']}")

    section = build_section(folder, fw, cpp, clang)
    row = summary_row(folder, fw, cpp, clang)

    if dry_run:
        print("\n--- section (dry-run) ---")
        print(section)
        print("--- summary row ---")
        print(row)
        return

    if not SQCSA.exists():
        print(f"[WARN] {SQCSA} not found — writing section to stdout")
        print(section)
        return

    patch_sqcsa(folder, section, row, SQCSA)
    print(f"  Gate: {gate_status(fw, cpp, clang, folder)}")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Update SQCSA.txt sections by running static-analysis tools."
    )
    ap.add_argument(
        "folders",
        nargs="+",
        metavar="rel-folder",
        help="Folder(s) relative to the project root, e.g. src/kernel",
    )
    ap.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would be written without touching SQCSA.txt",
    )
    args = ap.parse_args()

    for folder in args.folders:
        process_folder(folder, dry_run=args.dry_run)

    print(f"\n✓ Done.  {SQCSA.relative_to(ROOT)} updated.")


if __name__ == "__main__":
    main()

