#!/bin/bash
# Assembles package/ (what goes on the card: Psychosomnium.sh + psychosomnium/). Needs build_sf3500.sh output.
set -e
cd "$(dirname "$0")"
BIN=build-sf3500/psychosomnium.stripped.mipsel
[ -f "$BIN" ] || { echo "build first: ./build_sf3500.sh"; exit 1; }
P=package/psychosomnium
mkdir -p "$P/gamedata" "$P/licenses"
cp package-src/Psychosomnium.sh package/Psychosomnium.sh
cp package-src/run.sh package-src/README.txt package-src/NOTICE.md "$P/"
cp package-src/licenses/* "$P/licenses/"
cp package-src/boxart.png "$P/boxart.png"
cp "$BIN" "$P/psychosomnium.mipsel"
# language packs (tools/lang/build_packs.py): .pak = menu texts + glyphs (in the package); the dialogue translations (.script) are a translation of the
# author's text and ship as a separate OPTIONAL download, dist/psychosomnium-dialogue-translations.zip, that the launcher reads from the lang folder
mkdir -p "$P/lang" dist
cp build-lang/*.pak "$P/lang/"
rm -f "$P"/lang/*.script "$P"/lang/*.zip
rm -f dist/psychosomnium-dialogue-translations.zip
~/venvs/pydev/bin/python -I - <<'PYEOF'
import glob, os, zipfile
z = zipfile.ZipFile('dist/psychosomnium-dialogue-translations.zip', 'w', zipfile.ZIP_DEFLATED)
z.write('package-src/DIALOGUE-TRANSLATIONS-README.txt', 'README.txt')
for f in sorted(glob.glob('build-lang/*.script')): z.write(f, os.path.basename(f))
z.close()
print('optional download: dist/psychosomnium-dialogue-translations.zip')
PYEOF
true  # (licenses/ already carries the Noto notice from package-src/licenses)
cat > "$P/gamedata/PUT-PSYCHO.EXE-HERE.txt" <<'EOT'
Put psycho.exe (or psycho1.zip) in this folder, then start the port.
The first start unpacks the game data into the "cache" folder here.
EOT
chmod +x package/Psychosomnium.sh "$P/run.sh" "$P/psychosomnium.mipsel"
md5sum "$P/psychosomnium.mipsel"
echo "package ready: package/"
