#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Builds a standalone "dist/" folder: the shippable Emerald Launcher (Windows),
# separated from the source tree. The end user only needs this folder.
#
# Run from the MSYS2 *mingw64* shell, after building:
#     cmake --build build
#     ./package.sh
# ---------------------------------------------------------------------------
set -e
cd "$(dirname "$0")"

DIST=dist
MINGW=/mingw64/bin

echo ">> Cleaning $DIST/"
rm -rf "$DIST"
mkdir -p "$DIST"/roms "$DIST"/saves "$DIST"/system

echo ">> Copying app + assets"
cp build/emerald_launcher.exe "$DIST"/
cp -r imgs sounds cores maker "$DIST"/
cp alagard.ttf db.json        "$DIST"/
[ -f branding.json ] && cp branding.json "$DIST"/
[ -d branding ]      && cp -r branding "$DIST"/   # custom icon/splash/bg/font referenced by branding.json

echo ">> Bundling runtime DLLs (recursive)"
# The SDL satellite libs are loaded by the exe; resolve every mingw64 DLL that
# the exe, the SDL libs and the cores depend on, and copy them in.
cp -f "$MINGW"/SDL2.dll "$MINGW"/SDL2_image.dll "$MINGW"/SDL2_ttf.dll "$DIST"/ 2>/dev/null || true
{
    ldd "$DIST"/emerald_launcher.exe
    ldd "$MINGW"/SDL2_image.dll
    ldd "$MINGW"/SDL2_ttf.dll
    for c in "$DIST"/cores/*.dll; do [ -f "$c" ] && ldd "$c"; done
} 2>/dev/null | grep -oiE '/mingw64/bin/[^ ]+\.dll' | sort -u | while read -r dll; do
    cp -nf "$dll" "$DIST"/ 2>/dev/null || true
done

echo ">> Writing README"
cat > "$DIST"/README.txt <<'EOF'
Emerald Launcher
================
Doble clic en  emerald_launcher.exe  para abrir.

Carpetas donde poner tus cosas:
  roms/     Tus ROMs, en subcarpetas (ej. roms/Z1/Zelda1.nes)
  system/   BIOS (ej. CD-i:  system/same_cdi/bios/cdimono1.zip)
  saves/    Se generan solos al jugar

Archivos:
  db.json   Base de datos de juegos. Edítala con el editor en  maker/
  cores/    Emuladores (no tocar)

Controles del launcher:
  Flechas   Navegar      Enter  Seleccionar
  Tab       Ajustes      F11    Maximizar ventana
  Esc       En juego: volver al launcher
EOF

echo ">> Done -> $DIST/  ($(du -sh "$DIST" 2>/dev/null | cut -f1))"
