#!/usr/bin/env python3
"""Compare two FX sweep tables (see README.md).

    compare.py a.tsv b.tsv [--tolerance 0.5]

Lists rows whose difference-from-default or level differ by more than the
tolerance, flags present in only one table, and controls dead in one and live
in the other. Exit status 1 if anything differs.
"""
import csv
import sys


def load(path):
    rows = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            rows[(row["section"], row["control"], row["value"])] = row
    return rows


def number(text):
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    tolerance = 0.5
    if "--tolerance" in sys.argv:
        tolerance = float(sys.argv[sys.argv.index("--tolerance") + 1])
    a, b = load(args[0]), load(args[1])
    problems = []
    for key in sorted(set(a) | set(b)):
        if key not in a or key not in b:
            problems.append(f"only in {'first' if key in a else 'second'}: {' | '.join(key)}")
            continue
        ra, rb = a[key], b[key]
        for column in ("vs_default_db", "level_db", "vs_off_db"):
            va, vb = number(ra[column]), number(rb[column])
            if va is None or vb is None:
                continue
            # Below -60 dB both are "no difference": compare the verdict, not the noise.
            if va < -60 and vb < -60:
                continue
            if abs(va - vb) > tolerance:
                problems.append(f"{' | '.join(key)}: {column} {va:.2f} vs {vb:.2f}")
        fa, fb = set(filter(None, ra["flags"].split(","))), set(filter(None, rb["flags"].split(",")))
        if fa != fb:
            problems.append(f"{' | '.join(key)}: flags {sorted(fa)} vs {sorted(fb)}")
    for p in problems:
        print(p)
    print(f"{len(problems)} difference(s) over {len(set(a) & set(b))} common rows (tolerance {tolerance} dB)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
