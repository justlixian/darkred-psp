#!/bin/sh
# Build DARKRED.iso from the pieces produced by `make -f Makefile.psp bundle` (out/iso_parts).
# needs: genisoimage (or mkisofs / xorriso)
set -e
PARTS=${1:-out/iso_parts}; OUT=${2:-DARKRED.iso}
rm -rf iso_tree && mkdir -p iso_tree/PSP_GAME/SYSDIR iso_tree/PSP_GAME/USRDIR
cp "$PARTS/PARAM.SFO" "$PARTS/ICON0.PNG" "$PARTS/PIC1.PNG" iso_tree/PSP_GAME/
cp "$PARTS/EBOOT.BIN" iso_tree/PSP_GAME/SYSDIR/
cp "$PARTS/darkred.pak" iso_tree/PSP_GAME/USRDIR/
if command -v genisoimage >/dev/null; then MK=genisoimage; elif command -v mkisofs >/dev/null; then MK=mkisofs; else MK="xorriso -as mkisofs"; fi
$MK -iso-level 4 -xa -A "PSP GAME" -V DARKRED -sysid "PSP GAME" -volset DARKRED -publisher DARKRED -o "$OUT" iso_tree
echo "wrote $OUT"
