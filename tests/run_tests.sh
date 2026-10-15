#!/bin/sh
# Headless regression tests for the DARKRED runtime.  usage: sh tests/run_tests.sh   (from the project root)
# Needs: ./darkred_null (make null) and assets/darkred.pak (python3 tools/pack.py data.win assets/darkred.pak)
cd "$(dirname "$0")/.." || exit 1
BIN=./darkred_null; PAK=${PAK:-assets/darkred.pak}; OUT=$(mktemp -d)
export ASAN_OPTIONS=detect_leaks=0
fail=0
ok()   { echo "  PASS  $1"; }
bad()  { echo "  FAIL  $1"; fail=1; }
sum()  { md5sum "$1" | cut -d' ' -f1; }

[ -x $BIN ] || { echo "build first: make null"; exit 1; }
[ -f "$PAK" ] || { echo "missing $PAK"; exit 1; }

echo "1. golden frames (title -> START -> map -> location dialogue)"
$BIN $PAK --frames 400 --click 40:94,132 --click 260:42,86 \
  --dump 35:$OUT/title.ppm --dump 250:$OUT/map.ppm --dump 330:$OUT/dialogue.ppm --data $OUT/ 2>$OUT/err1.txt
for n in title map dialogue; do
  want=$(grep "^$n " tests/golden.md5 | cut -d' ' -f2)
  [ "$(sum $OUT/$n.ppm)" = "$want" ] && ok "frame: $n" || bad "frame: $n (got $(sum $OUT/$n.ppm), want $want)"
done
[ -s $OUT/err1.txt ] && bad "stderr not empty: $(head -c 200 $OUT/err1.txt)" || ok "no VM warnings"

echo "2. gamepad navigation snaps to buttons and markers"
$BIN $PAK --frames 300 --nav 20:0,1 --press 25 --nav 270:0,-1 --nav 275:0,-1 --data $OUT/ 2>$OUT/nav.txt
grep -q "frame 20 -> (94,133)" $OUT/nav.txt && grep -q "frame 275 -> (102,61)" $OUT/nav.txt && ok "d-pad hotspot navigation" || bad "d-pad navigation: $(cat $OUT/nav.txt | head -5)"

echo "3. audio mixer renders the click sound at the right moment"
$BIN $PAK --frames 100 --click 40:94,132 --wav $OUT/a.wav --data $OUT/ 2>/dev/null
python3 - "$OUT/a.wav" <<'PY' && ok "click sfx starts on frame 40" || bad "audio"
import sys, wave, struct
w = wave.open(sys.argv[1]); d = struct.unpack('<%dh' % (w.getnframes() * 2), w.readframes(w.getnframes()))
first = next(i for i, s in enumerate(d[0::2]) if abs(s) > 50)
assert 40 * 735 <= first < 41 * 735 and max(map(abs, d)) > 500, first
PY

echo "4. soak: 12000 frames of scripted taps under ASan/UBSan"
$BIN $PAK --frames 12000 $(cat tests/soak_args.txt) --data $OUT/ 2>$OUT/soak.txt
rc=$?
[ $rc -eq 0 ] && [ ! -s $OUT/soak.txt ] && ok "no crashes, no VM warnings" || bad "soak rc=$rc: $(head -c 300 $OUT/soak.txt)"

echo "5. save / load / reset across separate runs"
SV=$OUT/save; mkdir -p $SV; ARGS="--click 40:94,132 --click 275:22,66"
for t in $(seq 340 60 6000); do ARGS="$ARGS --click $t:62,150"; done
$BIN $PAK --frames 6500 $ARGS --print 6400:game_progress --data $SV/ 2>$OUT/s1.txt
grep -q "game_progress = 1" $OUT/s1.txt && [ "$(cat $SV/save.sav 2>/dev/null)" = "1 " ] && ok "story event completes, progress saved" || bad "save: $(cat $OUT/s1.txt | head -3)"
$BIN $PAK --frames 300 --click 40:94,132 --print 20:game_progress --print 280:game_progress --data $SV/ 2>$OUT/s2.txt
grep -q "frame 20 game_progress = 0" $OUT/s2.txt && grep -q "frame 280 game_progress = 1" $OUT/s2.txt && ok "START loads saved progress" || bad "load: $(cat $OUT/s2.txt)"
$BIN $PAK --frames 300 --click 40:94,154 --print 280:game_progress --data $SV/ 2>$OUT/s3.txt
grep -q "frame 280 game_progress = 0" $OUT/s3.txt && [ ! -e $SV/save.sav ] && ok "RESET deletes the save" || bad "reset: $(cat $OUT/s3.txt)"

echo "6. PSP scaler (geometry, pixel mapping, colour order, no framebuffer overrun)"
gcc -std=gnu11 -O1 -fsanitize=address,undefined -o $OUT/test_psp_blit tests/test_psp_blit.c 2>/dev/null \
  && $OUT/test_psp_blit $OUT/title.ppm >/dev/null && $OUT/test_psp_blit >/dev/null && ok "1x and 1.5x blits are exact" || bad "psp blit"

rm -rf $OUT
[ $fail -eq 0 ] && echo "ALL TESTS PASSED" || { echo "SOME TESTS FAILED"; exit 1; }
