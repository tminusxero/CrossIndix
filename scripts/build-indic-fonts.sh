#!/bin/bash
# build-indic-fonts.sh --fonts-dir DIR --out DIR [--lock FILE] [--families "A B"] [--sizes 8,9,...] [--jobs N] [--verify SUMS]
# Builds the CrossIndix font folders (regular style, --intervals reading,<script> --shape auto) with
# this repository's converter and the Lipi submodule into OUT/<Family>/, copying <Family>.OFL.txt from
# the fonts dir as OUT/<Family>/OFL.txt. With --verify, compares the sha256 of every .cpfont against
# SUMS (format of sha256sum, paths <Family>/<Family>_<size>.cpfont) and exits 1 on any difference.
# Writes only under OUT. Source TTFs: scripts/fetch-indic-fonts.py puts them in the fonts dir.
set -u
R=$(cd "$(dirname "$0")/.." && pwd)
FONTS=""; OUT=""; LOCK=$R/scripts/indic-fonts.lock; FAM=""; SIZES=8,9,10,12,14,16,18,20; JOBS=2; VERIFY=""
while [ $# -gt 0 ]; do
  case "$1" in
    --fonts-dir) FONTS=$2; shift 2;; --out) OUT=$2; shift 2;; --lock) LOCK=$2; shift 2;;
    --families) FAM=$2; shift 2;; --sizes) SIZES=$2; shift 2;; --jobs) JOBS=$2; shift 2;; --verify) VERIFY=$2; shift 2;;
    *) echo "unknown arg $1" >&2; exit 2;;
  esac
done
[ -n "$FONTS" ] && [ -n "$OUT" ] || { echo "usage: see header" >&2; exit 2; }
FONTS=$(realpath "$FONTS"); OUT=$(realpath -m "$OUT"); mkdir -p "$OUT"
[ -n "$FAM" ] || FAM=$(grep -v '^#\|^commit\|^$' "$LOCK" | awk '{print $1}')
one() { # family
  local fam=$1 script file ttf
  script=$(awk -v f="$fam" '$1==f{print $2}' "$LOCK"); file=$(awk -v f="$fam" '$1==f{print $3}' "$LOCK")
  [ -n "$script" ] || { echo "$fam: not in $LOCK" >&2; return 1; }
  ttf=$FONTS/$(basename "$file")
  [ -f "$ttf" ] || { echo "$fam: missing $ttf" >&2; return 1; }
  rm -rf "$OUT/$fam"; mkdir -p "$OUT/.src"
  # The converter names its files after the TTF: give it <Family>.ttf so the folder holds <Family>_<size>.cpfont.
  ln -sfn "$ttf" "$OUT/.src/$fam.ttf"; ttf=$OUT/.src/$fam.ttf
  python3 "$R/lib/EpdFont/scripts/fontconvert_sdcard.py" "$ttf" --intervals "reading,$script" \
    --sizes "$SIZES" --shape auto --output-dir "$OUT/$fam" > "$OUT/$fam.log" 2>&1
  local rc=$?
  [ -f "$FONTS/$fam.OFL.txt" ] && cp "$FONTS/$fam.OFL.txt" "$OUT/$fam/OFL.txt"
  echo "$fam: exit=$rc files=$(ls "$OUT/$fam"/*.cpfont 2>/dev/null | wc -l) $(grep -m1 -o 'modes: .*' "$OUT/$fam.log")"
  return $rc
}
export -f one; export OUT R FONTS SIZES LOCK
printf '%s\n' $FAM | xargs -P "$JOBS" -I{} bash -c 'one {}'
rc=$?
if [ -n "$VERIFY" ]; then
  (cd "$OUT" && sha256sum */*.cpfont | sort -k2) > "$OUT/SHA256SUMS.txt"
  if diff <(sort -k2 "$VERIFY") "$OUT/SHA256SUMS.txt" > "$OUT/verify.diff"; then
    echo "verify: $(wc -l < "$OUT/SHA256SUMS.txt") files identical to $VERIFY"
  else
    echo "verify: DIFFERENCES against $VERIFY (see $OUT/verify.diff)"; cat "$OUT/verify.diff"; rc=1
  fi
fi
exit $rc
