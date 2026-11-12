#!/bin/sh
# Turn a Dragon Quest IX ROM into the PC game. Run by BUILD.bat inside MSYS2
# (UCRT64); also works on Linux with gcc, make, python3 (+ PyYAML) and SDL2.
#
#   tools/build-game.sh <rom.nds>
#
# Everything is made inside this folder:
#   build/   working files (the extracted code, the generated C, objects)
#   game/    the result: dqix.exe and everything it needs, including a copy
#            of the ROM, so the folder can be moved anywhere
set -e
cd "$(dirname "$0")/.."
ROM="$1"
[ -f "$ROM" ] || { echo "No ROM given (or it doesn't exist): $ROM"; exit 1; }
PY=python3; command -v $PY >/dev/null 2>&1 || PY=python
# a step's title; with DQIX_PROGRESS (BUILD.bat --prepare, for RecompLauncher)
# also a "progress <0-1> <step>" line for the launcher's setup screen
step() {    # step <fraction> <title>
    echo "== $2"
    [ -z "$DQIX_PROGRESS" ] || echo "progress $1 $2"
}

# a zipped ROM: the .nds inside it, taken out into build/
case "$ROM" in *.zip|*.ZIP)
    mkdir -p build
    $PY -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
n = [i for i in z.namelist() if i.lower().endswith(".nds")]
if not n: sys.exit("No .nds file in " + sys.argv[1])
open(sys.argv[2], "wb").write(z.read(n[0]))' "$ROM" build/rom-from-zip.nds
    ROM=build/rom-from-zip.nds ;;
esac

step 0.1 "Checking the ROM"
# no checksum: any version can be tried. The game code in the header says which it is.
CODE=$(dd if="$ROM" bs=1 skip=12 count=4 2>/dev/null | tr -cd 'A-Za-z0-9') || true
case "$CODE" in
YDQE) echo "   Dragon Quest IX (USA)" ;;
YDQP) echo "   Dragon Quest IX (Europe)" ;;
YDQJ) echo "   Dragon Quest IX (Japan): only the USA and European versions are supported; trying anyway" ;;
"")   echo "   Couldn't read the ROM file: $ROM"
      echo "   Is it still being copied, or open in another program? Is it a OneDrive file that isn't downloaded"
      echo "   (right-click it, Always keep on this device)? Then run this again."
      exit 1 ;;
*)    echo "   This doesn't look like Dragon Quest IX (game code '$CODE'); trying anyway" ;;
esac

step 0.15 "Extracting the game's code"
$PY tools/extract_rom.py "$ROM" build/extract
step 0.2 "Recompiling it to C (about a minute)"
# the symbol map and address lists are the USA version's; region.py moves them for the European one
REGION=$($PY tools/region.py "$ROM" build/extract build/region)
if [ "$REGION" = eur ]; then CFG=build/region; CONFIG=build/region/arm9; else CFG=.; CONFIG=config/usa/arm9; fi
# code compiled for the other version (or before this check existed) doesn't link with this one: start over
if [ "$(cat build/obj/region.txt 2>/dev/null)" != "$REGION" ]; then
    rm -rf build/recomp build/obj/gen
    mkdir -p build/obj && echo "$REGION" > build/obj/region.txt
fi
$PY recomp/build.py --extract build/extract --config $CONFIG --out build/recomp --hle $CFG/hle.txt --interpret $CFG/interpret.txt --patch $CFG/patches.txt --tags $CFG/tags.txt --hooks $CFG/hooks.txt
$PY recomp/arm7.py --extract build/extract --out build/recomp7 --hle hle7.txt

# The program's icon: the game's own (the one the DS menu shows), made from the
# ROM like everything else, so nothing from the game is kept in this project.
# Optional: without it the exe has Windows' plain icon.
make_icon() {    # make_icon <rom> <dir> -> sets RES
    RES=
    mkdir -p "$2"
    if $PY tools/rom_icon.py "$1" "$2/dqix.ico" && command -v windres >/dev/null 2>&1; then
        cat > "$2/dqix.rc" <<'RC'
1 ICON "dqix.ico"
1 VERSIONINFO
FILEVERSION 0,0,0,0
PRODUCTVERSION 0,0,0,0
FILEOS 0x40004
FILETYPE 1
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904B0"
    BEGIN
      VALUE "FileDescription", "Dragon Quest IX"
      VALUE "ProductName", "DQIX Enhanced"
      VALUE "OriginalFilename", "dqix.exe"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
RC
        if (cd "$2" && windres dqix.rc -O coff -o dqix_res.o); then RES="$2/dqix_res.o"; echo "   icon: the game's own"; fi
    fi
}

step 0.3 "Compiling (the first time takes a while; later builds only redo what changed)"
mkdir -p game
case "$(uname -s)" in
MINGW*|MSYS*)
    # linked in build/, then put in place in one go: Explorer reads the icon of
    # the desktop shortcut from dqix.exe, and reading it while the linker was
    # still writing it left a black square there (cached until Windows forgets)
    EXE=build/pc/dqix.exe
    mkdir -p build/pc
    make_icon "$ROM" build/icon
    # dqixcore.dll: the game again as a library, for the other players' DSes in a session
    make -j"$(nproc)" -f build.mk OS=Windows_NT GEN=build/recomp GEN7=build/recomp7 OBJ=build/obj FRONTEND=sdl OUT="$EXE" CORE=game/dqixcore.dll \
         CFLAGS="-O1 -w" DEFS=-DRECOMP_TRACE SDL_CFLAGS="$(sdl2-config --cflags)" SDL_LIBS="-lSDL2" \
         LDFLAGS="-static-libgcc -Wl,--stack,67108864" RES="$RES" "$EXE" core
    if ! cmp -s "$EXE" game/dqix.exe; then
        cp "$EXE" game/dqix.exe.new && mv -f game/dqix.exe.new game/dqix.exe
        # the shortcuts' icon read again from the new program (Windows 10/11; harmless if missing)
        cmd //c "ie4uinit.exe -show" >/dev/null 2>&1 || true
    fi
    cp /ucrt64/bin/SDL2.dll game/ ;;
*)
    EXE=game/dqix
    make -j"$(nproc)" -f build.mk GEN=build/recomp GEN7=build/recomp7 OBJ=build/obj FRONTEND=sdl OUT="$EXE" CORE=game/dqixcore.so PIC=-fPIC \
         CFLAGS="-O1 -w" DEFS=-DRECOMP_TRACE SDL_CFLAGS="${SDL_CFLAGS:-$(sdl2-config --cflags)}" SDL_LIBS="${SDL_LIBS:-$(sdl2-config --libs)}" "$EXE" core ;;
esac

step 0.9 "Putting the game folder together"
mkdir -p game/data game/skins game/fonts game/lang
rm -rf game/data/code && cp -r build/extract game/data/code     # the overlays (the game checks which is loaded)
if ! cmp -s "$ROM" game/data/dqix.nds; then cp "$ROM" game/data/dqix.nds; fi
cp skins/*.png skins/*.txt game/skins/ 2>/dev/null || true
cp fonts/*.ttf fonts/*.txt game/fonts/ 2>/dev/null || true             # the HD text's fonts (Graphics > Text font)
cp lang/*.txt game/lang/ 2>/dev/null || true                          # the menus' and messages' translations (Language menu)
# modern job icons, in the HD texture pack (runtime/hdsprite.h): made from the ROM
# like the program's icon. Optional: without them the game shows its own.
$PY tools/job_icons.py "$ROM" game/textures/jobs || echo "   job icons: not made (optional)"
# the trophies' pictures (runtime/trophies.c): their RetroAchievements badges,
# fetched from the site once, for this computer only (never kept in the project).
# Optional: without them (no Internet) the game shows its cups.
$PY tools/trophy_badges.py game/trophies || echo "   trophy pictures: not fetched (optional)"
# townspeople in 3D (runtime/npc3d.c, Graphics > Townspeople in 3D): made from
# the player's own Dragon Quest VII and/or VIII (3DS), put in rom/3DS (a
# decrypted ROM: .3ds, .cci, .cxi, .cia, or a folder already extracted), or
# folders named in rom/dq7-path.txt and rom/dq8-path.txt; and from this ROM's
# own characters. Remade when those change. Never kept in the project. Optional.
mkdir -p rom/3DS
gamedir() {
    [ -f "rom/$1-path.txt" ] || return 0
    local d; d="$(tr -d '\r' < "rom/$1-path.txt")"
    case "$(uname -s)" in MINGW*|MSYS*) d="$(cygpath -u "$d")" ;; esac
    if [ -d "$d" ]; then echo "$d"; fi
}
DQ7="$(gamedir dq7)"; DQ8="$(gamedir dq8)"
if [ ! -f game/npc3d/map.txt ] || [ -n "$(find tools/npc3d rom/dq7-path.txt rom/dq8-path.txt rom/3DS -newer game/npc3d/map.txt 2>/dev/null)" ]; then
    echo "== Townspeople in 3D (from Dragon Quest VII / VIII / IX)"
    if [ -z "$DQ7" ] || [ -z "$DQ8" ]; then                  # rom/3DS: what the paths don't give
        while read -r g d; do
            case "$g" in dq7) [ -n "$DQ7" ] || DQ7="$d" ;; dq8) [ -n "$DQ8" ] || DQ8="$d" ;; esac
        done <<EOF3DS
$($PY tools/npc3d/rom3ds.py rom/3DS build/npc3d-src)
EOF3DS
    fi
    $PY tools/npc3d/make_all.py "${DQ7:--}" "${DQ8:--}" game/npc3d "$ROM" || echo "   townspeople in 3D: not made (optional; needs Python's numpy)"
fi
# the same plaques for the modern Equip screen (runtime/frontend_sdl.c equip_pieces:
# the bar, the card) and the jobs' coats of arms for the shop's wall; its backdrop, ui/equip-shop.png, if there
mkdir -p game/ui/jobs game/ui/crests && cp tools/job_icons_plate/*.png game/ui/jobs/ 2>/dev/null || true
cp tools/job_crests/*.png game/ui/crests/ 2>/dev/null || true
[ -f game/ui/equip-shop.png ] || cp tools/ui/equip-shop.png game/ui/ 2>/dev/null || true
# HD textures for the whole game, without playing through it: where Real-ESRGAN
# is installed (game/textures-hd-outil), textures-rom takes every 3D texture out
# of the ROM into textures/dump (once per ROM), then textures-hd makes their HD
# versions in the background (only the ones the pack doesn't have yet).
case "$(uname -s)" in MINGW*|MSYS*)
    for t in textures-rom textures-hd; do
        if [ "tools/$t.c" -nt "game/$t.exe" ]; then gcc -O2 -I runtime -o "game/$t.exe" "tools/$t.c" -lm || echo "   $t.exe: not rebuilt (optional)"; fi
    done
    if [ -d game/textures-hd-outil ] && [ -f game/textures-rom.exe ]; then
        ./game/textures-rom.exe --auto || echo "   HD textures from the ROM: not made (optional)"
    fi ;;
esac
# optional files: the built-in Wi-Fi server's downloads and DQVC schedule
for src in extras/wfc wfc; do
    for g in "$src"/*/; do
        [ -f "$g/_list.txt" ] || continue
        mkdir -p "game/wfc/$(basename "$g")" && cp "$g"* "game/wfc/$(basename "$g")/"
    done
done
# DQVC's news: the one message that survives (Nintendo's last), from the fans'
# archive of Nintendo's download server. The quests and DQVC's sales are
# built into the game; the news is Square Enix's text, so it is fetched, not kept here.
if [ "$REGION" = eur ]; then GC=YDQP; NEWS=infomes_45.bin; else GC=YDQE; NEWS=infomes_47.bin; fi
if [ ! -f game/wfc/$GC/$NEWS ]; then
    mkdir -p game/wfc/$GC
    if curl -fsSL --max-time 30 -o game/wfc/$GC/$NEWS \
         https://raw.githubusercontent.com/InternalLoss/DLS1-Files/master/$GC/$NEWS; then
        echo "   DQVC news: fetched"
    else
        rm -f game/wfc/$GC/$NEWS; echo "   DQVC news: not fetched (optional; Sellma just has no news)"
    fi
fi
cat > game/play.bat <<'BAT'
@echo off
rem Dragon Quest IX (PC). Keys: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R,
rem Enter = START, Right Shift = SELECT, mouse = touch screen, Tab = fast forward.
rem Every setting is in the menu bar at the top of the window.
cd /d "%~dp0"
dqix.exe data\dqix.nds data\code 0 1 dqix.sav
if errorlevel 1 pause
BAT
cat > game/play.sh <<'SH'
#!/bin/sh
cd "$(dirname "$0")" && exec ./dqix data/dqix.nds data/code 0 1 dqix.sav
SH
chmod +x game/play.sh
echo
[ -z "$DQIX_PROGRESS" ] || echo "progress 1 Done"
case "$(uname -s)" in MINGW*|MSYS*) echo "== Done. The game is in the 'game' folder: double-click dqix.exe." ;; *) echo "== Done. The game is in the 'game' folder: run play.sh." ;; esac
