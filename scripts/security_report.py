#!/usr/bin/env python3
"""
B-TRON OS — Security & Quality Code Audit Report Generator.

Runs static analysis (Flawfinder, Cppcheck, Clang Static Analyzer) across the
B-TRON codebase and outputs a structured ASCII source code tree report with
colored severity badges. Designed specifically for CI logs and terminal output,
with pure ASCII box characters and standard ANSI colors that render cleanly
without broken escape sequences or missing glyphs.

Usage:
    python3 scripts/security_report.py [options] [target_folders ...]

Options:
    --color=auto|always|never   Control color output (default: auto)
    --no-color                  Disable all color escapes
    --fast                      Skip heavy disassembler table cppcheck
    --all                       Scan entire src/ tree
"""

import argparse
import os
import re
import subprocess
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ── Target Module Configuration ──────────────────────────────────────────────

MODULE_CONFIGS = [
    {
        "folder": "src/workbench",
        "name": "B-System Workbench Desktop Menu Tracker About",
        "includes": ["-Iinclude", "-Iinclude/apps/workbench", "-Isrc/apps/workbench"],
        "excludes": [],
        "skip_clang": False,
        "slice": "slice-1",
    },
    {
        "folder": "src/kernel",
        "name": "T-Kernel 2.0 and B-System Run-Time C Library",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/kernel"],
        "excludes": [],
        "skip_clang": False,
        "slice": "slice-2",
    },
    {
        "folder": "src/cores",
        "name": "Core Platform Builds",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/kernel", "-Iinclude/drivers/"],
        "excludes": [],
        "skip_clang": False,
        "slice": "slice-3",
    },
    {
        "folder": "src/drivers",
        "name": "Hardware Device Drivers",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Iinclude/drivers", "-Iinclude/drivers/broadcom"],
        "excludes": [],
        "skip_clang": True,  # driver headers expect cross-target registers
        "slice": "slice-4",
    },
    {
        "folder": "src/fs",
        "name": "Virtual Filesystem & Block Storage",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Iinclude/drivers/broadcom"],
        "excludes": [],
        "skip_clang": False,
        "slice": "slice-5",
    },
    {
        "folder": "src/apps/terminal",
        "name": "B-System BTRON 3.20 TERMIOS POSIX Terminal",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu"],
        "excludes": ["src/apps/clu/be","src/apps/clu/sc","src/apps/clu/sh","src/apps/clu/tv"],
        "skip_clang": False,
        "slice": "slice-6",
    },
    {
        "folder": "src/apps/clu",
        "name": "CLU BTRON Commands and Utilities",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu"],
        "excludes": ["src/apps/clu/be","src/apps/clu/sc","src/apps/clu/sh","src/apps/clu/tv"],
        "skip_clang": False,
        "slice": "slice-7",
    },
    {
        "folder": "src/apps/clu/be",
        "name": "Binary Editor & Disassemblers",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu/be", "-Isrc/apps/clu/be"],
        "excludes": [],
        "clang_core_only": True,
        "skip_clang": False,
        "slice": "slice-7",
    },
    {
        "folder": "src/apps/clu/sc",
        "name": "Sokhatsky Commander",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu/sc", "-Isrc/apps/clu/sc"],
        "excludes": [],
        "clang_core_only": True,
        "skip_clang": False,
        "slice": "slice-7",
    },
    {
        "folder": "src/apps/clu/sh",
        "name": "POSIX Shell",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu/sh", "-Isrc/apps/clu/sh"],
        "excludes": [],
        "clang_core_only": True,
        "skip_clang": False,
        "slice": "slice-7",
    },
    {
        "folder": "src/apps/clu/tv",
        "name": "Terminal Vision Console Editor",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu/tv", "-Isrc/apps/clu/tv"],
        "excludes": [],
        "clang_core_only": True,
        "skip_clang": False,
        "slice": "slice-7",
    },
    {
        "folder": "src/apps/chokanji",
        "name": "Chokanji Commands and Utilities",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/clu"],
        "excludes": [],
        "skip_clang": False,
        "slice": "slice-8",
    },
    {
        "folder": "src/apps/volumes",
        "name": "TAD Volumes",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/cabinet", "-Isrc/apps/volumes"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-9",
    },
    {
        "folder": "src/apps/cabinet",
        "name": "TAD Cabinet Explorer",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/cabinet", "-Isrc/apps/cabinet"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-10",
    },
    {
        "folder": "src/apps/browser",
        "name": "TAD Browser",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/browser", "-Isrc/apps/browser"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-11",
    },
    {
        "folder": "src/apps/editor",
        "name": "TAD Editor",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/browser", "-Isrc/apps/browser"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-12",
    },
    {
        "folder": "src/apps/clarity",
        "name": "TAD DPS",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/apps/browser", "-Isrc/apps/browser"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-13",
    },
    {
        "folder": "src/desktop/font",
        "name": "B-Sysetm Font Manager",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/font", "-Isrc/desktop/font"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-14",
    },
    {
        "folder": "src/desktop/graphics",
        "name": "B-Sysetm BTRON 3.20 Display Primitives",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/graphics", "-Isrc/desktop/graphics"],
        "excludes": ["src/desktop/graphics/eegs","src/desktop/graphics/gles","src/desktop/graphics/tinygl","src/desktop/graphics/v3d","src/desktop/graphics/virgl"],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-15",
    },
    {
        "folder": "src/desktop/graphics/eegs",
        "name": "OpenGL ES PlayStation 2 EE/GS Backend",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/graphics/virgl", "-Isrc/desktop/graphics/virgl"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-16",
    },
    {
        "folder": "src/desktop/graphics/gles",
        "name": "OpenGL ES Graphics Library for Embedded Systems",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/graphics/gles", "-Isrc/desktop/graphics/gles"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-17",
    },
    {
        "folder": "src/desktop/graphics/v3d",
        "name": "OpenGL ES V3D/VC4 Backend",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/graphics/v3d", "-Isrc/desktop/graphics/v3d"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-18",
    },
    {
        "folder": "src/desktop/graphics/virgl",
        "name": "OpenGL ES VirtIO Backend",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/graphics/virgl", "-Isrc/desktop/graphics/virgl"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-19",
    },
    {
        "folder": "src/desktop/industrial/hmi",
        "name": "BTRON 3.20 HMI Library",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/industrial/hmi", "-Isrc/desktop/industrial/hmi"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-20",
    },
    {
        "folder": "src/desktop/industrial/segui",
        "name": "FOMA Library",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/industrial/segui", "-Isrc/desktop/industrial/segui"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-21",
    },
    {
        "folder": "src/desktop/tip",
        "name": "BTRON 3.20 TIP",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/tip", "-Isrc/desktop/tip"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-22",
    },
    {
        "folder": "src/desktop/window",
        "name": "BTRON 3.20 Window Manager",
        "includes": ["-Iinclude", "-Iinclude/rt", "-Isrc/desktop/window", "-Isrc/desktop/window"],
        "excludes": [],
        "clang_core_only": False,
        "skip_clang": False,
        "slice": "slice-23",
    },
]

# ── Color & Formatter ────────────────────────────────────────────────────────

class Formatter:
    def __init__(self, mode="auto"):
        self.color_enabled = self._eval_color(mode)

    @staticmethod
    def _eval_color(mode):
        if mode == "never" or os.environ.get("NO_COLOR"):
            return False
        if mode == "always":
            return True
        # auto: check CI or TTY
        if os.environ.get("CI") or os.environ.get("GITHUB_ACTIONS"):
            return True
        return hasattr(sys.stdout, "isatty") and sys.stdout.isatty()

    def c(self, code, text):
        if not self.color_enabled:
            return text
        return f"\033[{code}m{text}\033[0m"

    def bold(self, text): return self.c("1", text)
    def red(self, text): return self.c("1;31", text)
    def green(self, text): return self.c("1;32", text)
    def yellow(self, text): return self.c("1;33", text)
    def cyan(self, text): return self.c("1;36", text)
    def blue(self, text): return self.c("1;34", text)
    def dim(self, text): return self.c("2", text)


# ── Subprocess Runner ────────────────────────────────────────────────────────

def run_cmd(cmd, cwd=ROOT):
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
        return r.stdout, r.stderr, r.returncode
    except FileNotFoundError:
        return "", f"command not found: {cmd[0]}", -1


def tool_exists(tool_name):
    _, _, rc = run_cmd(["which", tool_name])
    return rc == 0


# ── Finding Collectors ───────────────────────────────────────────────────────

class Finding:
    def __init__(self, tool, file_path, line, severity, category, message):
        self.tool = tool               # 'flawfinder', 'cppcheck', 'clang'
        self.file_path = file_path     # relative to ROOT
        self.line = line
        self.severity = severity       # 'CRITICAL', 'WARNING', 'INFO', 'PORTABILITY'
        self.category = category       # rule name or CWE
        self.message = message


def collect_flawfinder(folder, excludes):
    if not tool_exists("flawfinder"):
        return []

    cmd = ["flawfinder", "--minlevel=4", "--quiet"]
    for ex in excludes:
        cmd.extend(["--exclude", f"*{ex}*"])
    cmd.append(str(ROOT / folder))

    stdout, _, _ = run_cmd(cmd)
    findings = []

    for line in stdout.splitlines():
        line = line.strip()
        m = re.match(r"^(.*?):(\d+):\s*\[(\d+)\]\s*\((.*?)\)\s*(.*?):\s*(.*)$", line)
        if m:
            fpath, line_no, lvl, cat, name, msg = m.groups()
            rel = fpath.replace(str(ROOT) + "/", "")
            lvl_int = int(lvl)
            sev = "CRITICAL" if lvl_int >= 5 else "WARNING"
            findings.append(Finding(
                tool="flawfinder",
                file_path=rel,
                line=int(line_no),
                severity=sev,
                category=f"[{cat}] {name}",
                message=msg.strip(),
            ))
    return findings


def collect_cppcheck(folder, includes, excludes, fast=False):
    if not tool_exists("cppcheck"):
        return []

    cmd = ["cppcheck", "-j4", "--std=c99", "--enable=warning,portability", "--quiet"]
    cmd.extend(includes)
    for ex in excludes:
        cmd.extend(["-i", str(ROOT / ex)])
    if fast and folder == "src/apps/clu/be":
        cmd.extend(["-i", str(ROOT / "src/apps/clu/be/arch")])
    cmd.append(str(ROOT / folder))

    stdout, stderr, _ = run_cmd(cmd)
    raw = stdout + stderr
    findings = []

    for line in raw.splitlines():
        line = line.strip()
        if not line or ": " not in line:
            continue
        # Filter tool encoding false positives on Japanese comments in headers
        if "unhandledChar" in line:
            continue
        if "syntaxError" in line and "termios.h" in line:
            continue

        m = re.match(r"^(.*?):(\d+):(?:\d+:)?\s*(error|warning|portability|style):\s*(.*?)(?:\s*\[(.*?)\])?$", line)
        if m:
            fpath, line_no, sev_str, msg, rule = m.groups()
            rel = fpath.replace(str(ROOT) + "/", "")
            if sev_str == "error":
                sev = "CRITICAL"
            elif sev_str == "warning":
                sev = "WARNING"
            else:
                sev = "PORTABILITY"
            findings.append(Finding(
                tool="cppcheck",
                file_path=rel,
                line=int(line_no),
                severity=sev,
                category=rule or sev_str,
                message=msg.strip(),
            ))
    return findings


def collect_clang(folder_info):
    if folder_info.get("skip_clang", False) or not tool_exists("clang"):
        return []

    folder = folder_info["folder"]
    full_path = ROOT / folder

    if folder_info.get("is_file", False):
        c_files = [full_path]
    else:
        if folder_info.get("clang_core_only", False):
            c_files = [f for f in sorted(full_path.rglob("*.c"))
                       if "arch/" not in str(f) and not f.name.endswith("test.c")]
        else:
            c_files = [f for f in sorted(full_path.rglob("*.c"))
                       if not f.name.endswith("test.c") and not any(ex in str(f) for ex in folder_info.get("excludes", []))]

    if not c_files:
        return []

    cmd = ["clang", "--analyze", "-std=c99", "-Wall", "-Wextra"]
    cmd.extend(folder_info.get("includes", []))
    cmd.extend(["-Xanalyzer", "-analyzer-output=text"])
    cmd.extend([str(f) for f in c_files])

    stdout, stderr, _ = run_cmd(cmd)
    raw = stdout + stderr
    findings = []

    for line in raw.splitlines():
        line = line.strip()
        if "warning:" in line or "error:" in line:
            m = re.match(r"^(.*?):(\d+):(?:\d+:)?\s*(warning|error):\s*(.*)$", line)
            if m:
                fpath, line_no, sev_str, msg = m.groups()
                rel = fpath.replace(str(ROOT) + "/", "")
                sev = "CRITICAL" if sev_str == "error" else "WARNING"
                findings.append(Finding(
                    tool="clang",
                    file_path=rel,
                    line=int(line_no),
                    severity=sev,
                    category="clang-analyzer",
                    message=msg.strip(),
                ))
    return findings


# ── Tree Hierarchy Builder ───────────────────────────────────────────────────

def build_tree_structure(all_files, findings_by_file):
    """
    Builds nested tree dict:
    tree = {
      'src': {
        'kernel': {
          'libstr.c': [findings],
          ...
        }
      }
    }
    """
    tree = {}
    for f in all_files:
        parts = Path(f).parts
        curr = tree
        for p in parts[:-1]:
            curr = curr.setdefault(p, {})
        curr[parts[-1]] = findings_by_file.get(f, [])
    return tree


# ── ASCII Tree Printer ───────────────────────────────────────────────────────

def format_badge(findings, fmt: Formatter, is_arch=False):
    crits = sum(1 for f in findings if f.severity == "CRITICAL")
    warns = sum(1 for f in findings if f.severity == "WARNING")
    ports = sum(1 for f in findings if f.severity == "PORTABILITY")

    if crits > 0:
        return fmt.red(f"[FAIL: {crits} crit, {warns} warn]")
    if warns > 0:
        if is_arch:
            return fmt.green(f"[PASS* {warns} warn (slice-8)]")
        return fmt.yellow(f"[WARN: {warns} issues]")
    if ports > 0:
        return fmt.dim(f"[INFO: {ports} port]")
    return fmt.green("[PASS]")


def print_tree(tree, fmt: Formatter, prefix=""):
    items = sorted(tree.items(), key=lambda x: (isinstance(x[1], list), x[0]))
    total = len(items)

    for idx, (name, val) in enumerate(items):
        is_last = (idx == total - 1)
        connector = "\\-- " if is_last else "+-- "
        next_prefix = prefix + ("    " if is_last else "|   ")

        if isinstance(val, dict):
            # Directory node
            print(f"{prefix}{connector}{fmt.bold(fmt.blue(name + '/'))}")
            print_tree(val, fmt, next_prefix)
        else:
            # File node with findings list
            findings = val
            is_arch = "/arch/" in name or "arch" in prefix
            badge = format_badge(findings, fmt, is_arch=is_arch)

            # Dot leader
            line_lead = f"{prefix}{connector}{fmt.bold(name)} "
            # compute display width without color escapes
            vis_len = len(f"{prefix}{connector}{name} ")
            dots_len = max(2, 60 - vis_len)
            dots = fmt.dim("." * dots_len)

            print(f"{line_lead}{dots} {badge}")

            # Print findings under this file
            if findings:
                f_total = len(findings)
                f_indent = next_prefix + "|   "
                for f_idx, f in enumerate(findings[:8]):
                    tool_tag = f"[{f.tool[:4].upper()}]"
                    if f.severity == "CRITICAL":
                        tag = fmt.red(f"{tool_tag}:CRIT")
                    elif f.severity == "WARNING":
                        tag = fmt.yellow(f"{tool_tag}:WARN")
                    else:
                        tag = fmt.dim(f"{tool_tag}:INFO")

                    cat_str = fmt.cyan(f"[{f.category}]")
                    msg_str = f.message
                    if len(msg_str) > 55:
                        msg_str = msg_str[:52] + "..."

                    print(f"{next_prefix}+-- {tag} L{f.line:<4} {cat_str} {msg_str}")

                if f_total > 8:
                    print(f"{next_prefix}\\-- {fmt.dim(f'... and {f_total - 8} additional findings')}")


# ── Summary Scoreboard Table ─────────────────────────────────────────────────

def print_scoreboard(folder_results, fmt: Formatter):
    w = 78
    sep_line = "+------------------------------+------------+------------+-----------+------------+"
    hdr_line = "+==============================+============+============+===========+============+"

    print(fmt.bold(hdr_line))
    print(fmt.bold("| Module / Folder              | Flawfinder | Cppcheck   | Clang     | Status     |"))
    print(fmt.bold(sep_line))

    grand_ff = 0
    grand_cpp = 0
    grand_cl = 0
    any_fail = False

    for item in folder_results:
        cfg = item["cfg"]
        findings = item["findings"]

        ff_cnt = sum(1 for f in findings if f.tool == "flawfinder")
        cpp_cnt = sum(1 for f in findings if f.tool == "cppcheck")
        cl_cnt = sum(1 for f in findings if f.tool == "clang")
        crit_cnt = sum(1 for f in findings if f.severity == "CRITICAL")

        grand_ff += ff_cnt
        grand_cpp += cpp_cnt
        grand_cl += cl_cnt

        if crit_cnt > 0:
            status = fmt.red("FAIL")
            any_fail = True
        elif ff_cnt > 0 or cpp_cnt > 0 or cl_cnt > 0:
            if cfg["folder"] == "src/apps/clu/be":
                status = fmt.green("PASS*")
            else:
                status = fmt.yellow("WARN")
        else:
            status = fmt.green("PASS")

        # Column formatting
        folder_col = f"{cfg['folder']:<28}"
        ff_col = f"{ff_cnt:<10}"
        cpp_col = f"{cpp_cnt:<10}"
        cl_col = "n/a" if cfg.get("skip_clang") else f"{cl_cnt:<9}"

        print(f"| {fmt.bold(folder_col)} | {ff_col} | {cpp_col} | {cl_col:<9} | {status:<10} |")

    print(fmt.bold(sep_line))
    overall_status = fmt.red("FAIL") if any_fail else fmt.green("PASS")
    print(f"| {fmt.bold('TOTAL SOURCE AUDIT')}           | {grand_ff:<10} | {grand_cpp:<10} | {grand_cl:<9} | {overall_status:<10} |")
    print(fmt.bold(hdr_line))
    print()


# ── Main Controller ──────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="B-TRON Security & Quality Audit Tree Report")
    parser.add_argument("folders", nargs="*", help="Specific target folder(s) to scan")
    parser.add_argument("--color", choices=["auto", "always", "never"], default="auto")
    parser.add_argument("--no-color", action="store_true", help="Disable ANSI colors")
    parser.add_argument("--fast", action="store_true", help="Fast mode (skip arch dasm cppcheck)")
    parser.add_argument("--all", action="store_true", help="Scan full src/ tree")
    args = parser.parse_args()

    fmt = Formatter("never" if args.no_color else args.color)

    # Banner
    w = 78
    print(fmt.bold("+" + "=" * w + "+"))
    print(fmt.bold(f"|  {fmt.cyan('B-TRON STATIC ANALYSIS & QUALITY AUDIT REPORT'):<87}|"))
    print(fmt.bold(f"|  Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S UTC'):<64}|"))
    print(fmt.bold("+" + "=" * w + "+"))
    print()

    # Determine folders to run
    configs_to_run = []
    if args.folders:
        for f in args.folders:
            f = f.rstrip("/")
            matched = [c for c in MODULE_CONFIGS if c["folder"] == f]
            if matched:
                configs_to_run.append(matched[0])
            else:
                configs_to_run.append({
                    "folder": f,
                    "name": f,
                    "includes": ["-Iinclude", "-Iinclude/rt"],
                    "excludes": [],
                    "skip_clang": False,
                })
    else:
        configs_to_run = MODULE_CONFIGS

    folder_results = []
    all_findings_by_file = defaultdict(list)
    scanned_files = set()

    print(fmt.bold("[-] Scanning source directories in tree sequence:"))
    for cfg in configs_to_run:
        folder = cfg["folder"]
        print(f"    +-- Scanning {fmt.bold(folder + '/')} ...", end="", flush=True)

        # Gather relevant C files for tree display
        full_p = ROOT / folder
        if full_p.is_file():
            c_files = [full_p]
        else:
            c_files = [f for f in sorted(full_p.rglob("*.c"))
                       if not any(ex in str(f) for ex in cfg.get("excludes", []))]

        for f in c_files:
            rel = str(f.relative_to(ROOT))
            scanned_files.add(rel)

        # Run analyzers
        ff_finds = collect_flawfinder(folder, cfg.get("excludes", []))
        cpp_finds = collect_cppcheck(folder, cfg.get("includes", []), cfg.get("excludes", []), fast=args.fast)
        cl_finds = collect_clang(cfg)

        comb = ff_finds + cpp_finds + cl_finds
        folder_results.append({
            "cfg": cfg,
            "findings": comb,
        })

        for item in comb:
            all_findings_by_file[item.file_path].append(item)

        print(f" {fmt.green('done')} ({len(comb)} findings)")

    print()
    print(fmt.bold("+" + "-" * w + "+"))
    print(fmt.bold(f"|  {fmt.cyan('SOURCE CODE TREE AUDIT'):<87}|"))
    print(fmt.bold("+" + "-" * w + "+"))

    # Build and print ASCII tree
    tree = build_tree_structure(scanned_files, all_findings_by_file)
    print_tree(tree, fmt)
    print()

    # Scoreboard
    print_scoreboard(folder_results, fmt)

    # Verdict Box
    total_crits = sum(sum(1 for f in r["findings"] if f.severity == "CRITICAL") for r in folder_results)
    print(fmt.bold("+" + "=" * w + "+"))
    if total_crits == 0:
        verdict = fmt.green("SECURITY & QUALITY GATE: PASSED")
        msg = "No critical level-5 flaws or static analysis errors detected."
        note = "* Arch disassembler hits in clu/be are catalogued in slice 8."
    else:
        verdict = fmt.red("SECURITY & QUALITY GATE: FAILED")
        msg = f"Detected {total_crits} critical errors requiring immediate resolution."
        note = "See tree breakdown above for line numbers and remediation details."

    print(fmt.bold(f"|  {verdict:<87}|"))
    print(fmt.bold(f"|  {msg:<76}|"))
    print(fmt.bold(f"|  {note:<76}|"))
    print(fmt.bold("+" + "=" * w + "+"))
    print()


if __name__ == "__main__":
    main()
