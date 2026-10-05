#!/usr/bin/env python3
"""Download the source TTFs named in scripts/indic-fonts.lock from the pinned
google/fonts commit, verify their sha256, and save them with their original
file names next to each font's OFL.txt (saved as <Family>.OFL.txt).

    python3 scripts/fetch-indic-fonts.py --lock scripts/indic-fonts.lock --out fonts/

Exit code 1 when a checksum differs; the printed line shows the new value so the
lock can be updated deliberately. Writes only under --out."""
import argparse
import hashlib
import os
import sys
import urllib.parse
import urllib.request

RAW = "https://raw.githubusercontent.com/google/fonts/{commit}/{path}"


def read_lock(path):
    commit, fonts = None, []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("commit "):
                commit = line.split()[1]
                continue
            family, script, gf_path, sha = line.split()
            fonts.append((family, script, gf_path, sha))
    if not commit or not fonts:
        sys.exit(f"{path}: needs a commit line and at least one font line")
    return commit, fonts


def fetch(url, dest):
    with urllib.request.urlopen(url, timeout=120) as r, open(dest, "wb") as f:
        f.write(r.read())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lock", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    commit, fonts = read_lock(a.lock)
    os.makedirs(a.out, exist_ok=True)
    bad = 0
    for family, _script, gf_path, want in fonts:
        name = os.path.basename(gf_path)
        dest = os.path.join(a.out, name)
        fetch(RAW.format(commit=commit, path=urllib.parse.quote(gf_path)), dest)
        got = hashlib.sha256(open(dest, "rb").read()).hexdigest()
        status = "ok" if got == want else "MISMATCH"
        bad += got != want
        print(f"{family}: {name} {got} {status}")
        ofl = os.path.join(a.out, f"{family}.OFL.txt")
        try:
            fetch(RAW.format(commit=commit, path=urllib.parse.quote(os.path.dirname(gf_path) + "/OFL.txt")), ofl)
        except Exception as e:  # noqa: BLE001 - the licence file is required for redistribution
            print(f"{family}: OFL.txt not fetched ({e})")
            bad += 1
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
