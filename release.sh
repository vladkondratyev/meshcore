#!/usr/bin/env bash
# Release of the companion firmware for Heltec T114, V3 and V4, built on this
# computer (GitHub Actions in the fork do not start).
#
#   ./release.sh            build the 8 files into release-out/ and check them
#   ./release.sh --publish  the same, then push, tag and make the GitHub release
#
# The version comes from src/helpers/FirmwareVersion.h (FORK_VERSION): raise it
# and describe the release in .github/release-notes.md before publishing.
# Only these 8 targets are built: all 46 boards take hours and heat the laptop.
set -euo pipefail
cd "$(dirname "$0")"

REMOTE=fork                         # git remote of the published repository
REPO=vladkondratyev/meshcore        # GitHub repository for the release
OUT=release-out
TARGETS=(
  Heltec_t114_companion_radio_ble
  Heltec_t114_companion_radio_usb
  Heltec_v3_companion_radio_ble
  Heltec_v3_companion_radio_usb
  Heltec_v3_companion_radio_wifi
  heltec_v4_companion_radio_ble
  heltec_v4_companion_radio_usb
  heltec_v4_companion_radio_wifi
)

command -v pio >/dev/null || { echo "pio not found: install PlatformIO (pip install platformio)"; exit 1; }
PUBLISH=0
[ "${1:-}" = "--publish" ] && PUBLISH=1

TAG=$(bash build.sh get-version)
if [ $PUBLISH = 1 ]; then
  git diff --quiet && git diff --cached --quiet || { echo "commit your changes first"; exit 1; }
  git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && { echo "tag $TAG already exists: raise FORK_VERSION"; exit 1; }
fi
HASH=$(git rev-parse --short HEAD)
echo "== $TAG ($HASH): ${#TARGETS[@]} targets"

rm -rf "$OUT"
mkdir -p "$OUT"
for t in "${TARGETS[@]}"; do
  echo "== $t"
  FIRMWARE_VERSION="$TAG" bash build.sh build-firmware "$t" > "$OUT/.log-$t.txt" 2>&1 || {
    echo "build failed, see $OUT/.log-$t.txt"; tail -20 "$OUT/.log-$t.txt"; exit 1; }
  cp out/* "$OUT"/   # build.sh empties out/ on every run
done

# every file must carry this version and commit
for f in "$OUT"/*.bin "$OUT"/*.uf2; do
  case "$f" in *-merged.bin) continue;; esac
  grep -qaF -- "$TAG-$HASH" "$f" || { echo "wrong version inside $f"; exit 1; }   # grep -a: binary file
done
rm -f "$OUT"/.log-*.txt
ls -1 "$OUT"
echo "== built $(ls "$OUT" | wc -l | tr -d ' ') files in $OUT/"

if [ $PUBLISH = 1 ]; then
  git push "$REMOTE" HEAD:main
  git tag "$TAG"
  git push "$REMOTE" "$TAG"
  gh release create "$TAG" -R "$REPO" --title "MeshCore Companion $TAG" \
    --notes-file .github/release-notes.md --latest "$OUT"/*
fi
