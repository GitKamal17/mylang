#!/usr/bin/env bash
#
# make_demo_gif.sh — renders docs/demo.gif
#
# Shows three phases for the GitHub release demo:
#   1. `mylang debug examples/game.vibes`  — real token dump scrolling in
#   2. `mylang run examples/game.vibes`    — compile + launch output
#   3. The game window appearing          — black canvas, bouncing ball
#
# Frames are rendered with scripts/demo_render.swift (AppKit/CoreGraphics,
# compiled once with swiftc) and assembled with ffmpeg's palette filters —
# no screen capture or third-party libraries required.
#
# Requires: swiftc (ships with macOS), ffmpeg (palettegen/paletteuse),
#           python3 (ball path).
#
# Usage: ./scripts/make_demo_gif.sh [output.gif]

set -euo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-docs/demo.gif}"
mkdir -p docs
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# ---- compile the Swift frame renderer once --------------------------------
if ! swiftc -O scripts/demo_render.swift -o "$TMP/render" 2>"$TMP/swift_err"; then
  echo "error: swiftc failed to build the frame renderer" >&2
  cat "$TMP/swift_err" >&2
  exit 1
fi
RENDER="$TMP/render"

FPS=10

# ---- capture real debug output for phase 1 --------------------------------
./mylang debug examples/game.vibes 2>&1 | head -24 > "$TMP/debug.txt" || true

# ---- frame counter --------------------------------------------------------
# IMPORTANT: i must increment in the MAIN shell. Never do i=$((i+1)) inside
# a $(...) command substitution — bash runs those in a subshell, so the
# increment would be lost and every frame would overwrite f0001.png.
# We use $(printf ...) only to FORMAT the current value (no mutation).
i=0
frame() { printf "%s/f%04d.png" "$TMP" "$i"; }

# ---- phase 1: mylang debug ------------------------------------------------
total=$(wc -l < "$TMP/debug.txt" 2>/dev/null || echo 0)
for reveal in 1 2 4 6 8 10 14 18; do
  if [ "$reveal" -gt "$total" ]; then break; fi
  head -n "$reveal" "$TMP/debug.txt" > "$TMP/chunk.txt"
  for hold in 1 2 3; do
    i=$((i + 1))
    "$RENDER" "$(frame)" "mylang debug examples/game.vibes" "$TMP/chunk.txt" 0 0 0
  done
done

# ---- phase 2: mylang run ---------------------------------------------------
cat > "$TMP/run.txt" << 'EOF'
> building with clang -std=c99 -Wall -Wextra
>   -I/opt/homebrew/include -L/opt/homebrew/lib
>   -lraylib -lm
> linking raylib...
> build OK -> /tmp/mylang_42
> running...
EOF
{
  echo "> mylang run examples/game.vibes"
  echo ""
  head -n 2 "$TMP/run.txt"
} > "$TMP/run1.txt"
{
  echo "> mylang run examples/game.vibes"
  echo ""
  head -n 5 "$TMP/run.txt"
} > "$TMP/run2.txt"
{
  echo "> mylang run examples/game.vibes"
  echo ""
  cat "$TMP/run.txt"
} > "$TMP/run3.txt"
for hold in 1 2 3; do
  i=$((i + 1))
  "$RENDER" "$(frame)" "mylang run examples/game.vibes" "$TMP/run1.txt" 0 0 0
done
for hold in 1 2 3; do
  i=$((i + 1))
  "$RENDER" "$(frame)" "mylang run examples/game.vibes" "$TMP/run2.txt" 0 0 0
done
for hold in 1 2 3; do
  i=$((i + 1))
  "$RENDER" "$(frame)" "mylang run examples/game.vibes" "$TMP/run3.txt" 0 0 0
done

# ---- phase 3: game window with bouncing ball --------------------------------
# Ball bounces inside the window (window x 40..840, y 60..500; ball 22px)
python3 - "$TMP/ball.txt" << 'PYEOF'
import sys
out = sys.argv[1]
x, y = 420.0, 260.0
vx, vy = 9.0, 6.5
pts = []
for _ in range(22):
    x += vx; y += vy
    if x < 50 or x > 818: vx = -vx
    if y < 70 or y > 478: vy = -vy
    x = max(50, min(818, x))
    y = max(70, min(478, y))
    pts.append((int(x), int(y)))
with open(out, "w") as f:
    for px, py in pts:
        f.write(f"{px} {py}\n")
PYEOF

for k in $(seq 1 22); do
  bx=$(sed -n "${k}p" "$TMP/ball.txt" | awk '{print $1}')
  by=$(sed -n "${k}p" "$TMP/ball.txt" | awk '{print $2}')
  i=$((i + 1))
  "$RENDER" "$(frame)" "mylang run examples/game.vibes" "$TMP/run3.txt" 1 "$bx" "$by"
done

# ---- assemble the GIF --------------------------------------------------------
ffmpeg -y -framerate "$FPS" -i "$TMP/f%04d.png" \
  -vf "split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse=dither=bayer:bayer_scale=5" \
  -loop 0 "$OUT" >/dev/null 2>&1

echo "Wrote $OUT ($(du -h "$OUT" | cut -f1), $i frames @ ${FPS}fps)"
