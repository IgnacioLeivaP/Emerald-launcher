#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Assembles dist-switch/ : the NRO + the sdmc:/emerald/ content folder.
# Run after building the NRO:
#   (devkitPro shell)  cd build-switch && cmake --build .
#   ./package-switch.sh
# Then copy onto the SD card:
#   dist-switch/emerald_launcher.nro  ->  sdmc:/switch/emerald_launcher.nro
#   dist-switch/emerald/              ->  sdmc:/emerald/
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"

DIST=dist-switch
echo ">> Cleaning $DIST/"
rm -rf "$DIST"
mkdir -p "$DIST/emerald/roms" "$DIST/emerald/saves"

echo ">> NRO"
if [ -f build-switch/emerald_launcher.nro ]; then
    cp build-switch/emerald_launcher.nro "$DIST/"
else
    echo "   WARN: build-switch/emerald_launcher.nro not found — build it first."
fi

echo ">> Content (db.json + roms, minus CD-i discs)"
cp db.json "$DIST/emerald/"     # root db.json = source of truth
[ -f branding.json ] && cp branding.json "$DIST/emerald/"
# Live ROMs/images live in dist/roms (moved there when packaging the PC build).
if [ -d dist/roms ] && [ -n "$(ls -A dist/roms 2>/dev/null)" ]; then
    ROMSRC="dist/roms"
else
    ROMSRC="roms"
fi
echo "   roms source: $ROMSRC"
cp -r "$ROMSRC/." "$DIST/emerald/roms/"
# CD-i is hidden on Switch (no core) — drop the heavy .bin/.cue discs.
find "$DIST/emerald/roms" -type f \( -iname '*.bin' -o -iname '*.cue' \) -delete

cat > "$DIST/README.txt" <<'EOF'
Instalación en Switch
=====================
Copia a la SD:
  emerald_launcher.nro  ->  sdmc:/switch/emerald_launcher.nro
  emerald/              ->  sdmc:/emerald/        (db.json + roms + saves)

Requisitos para que los juegos corran:
  - RetroArch instalado, con cores en  sdmc:/retroarch/cores/
    (los juegos se lanzan vía RetroArch chainload).

Notas:
  - CD-i no aparece en Switch (no hay core); Zelda's Adventure queda en su versión GBC.
  - Ship of Harkinian usa su .nro propio (ajusta la ruta en db.json si hace falta).
EOF

echo ">> Done -> $DIST/  ($(du -sh "$DIST/emerald" 2>/dev/null | cut -f1) de contenido)"