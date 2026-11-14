#!/bin/sh
# Dragon Quest IX for PC, on Linux and the Steam Deck: turns your own DQIX ROM
# into the PC game (BUILD.bat's twin). Run it in a terminal:
#
#   ./BUILD.sh                  the first .nds in the "rom" folder (or the last one used)
#   ./BUILD.sh <your ROM>       that ROM (.nds, or a .zip holding it)
#   ./BUILD.sh --prepare <ROM>  for RecompLauncher (recomplauncher.json "prepare"):
#                               also prints "progress <0-1> <step>" lines
#
# It needs Python 3 and SDL2, which SteamOS and most desktops already have.
# What else is missing (a C compiler, make, SDL2's headers, PyYAML) is fetched
# once into build/linux-tools, for this folder only: nothing is installed in
# the system and no password is asked. The result goes in the "game" folder:
# run game/play.sh (or game/dqix).
HERE="$(pwd)"                                   # a ROM path given relative to where this was run from
cd "$(dirname "$0")" || exit 1
abs() { case "$1" in ""|/*) printf '%s' "$1" ;; *) printf '%s/%s' "$HERE" "$1" ;; esac; }

ROM=
if [ "$1" = "--prepare" ]; then
    export DQIX_PROGRESS=1
    ROM="$(abs "$2")"
    [ -n "$ROM" ] && [ -f "$ROM" ] || { echo "No ROM given, or it doesn't exist: \"$2\""; exit 2; }
elif [ -n "$1" ]; then
    ROM="$(abs "$1")"
else
    # the ROM: the first .nds in the "rom" folder, or the one chosen last time
    for f in rom/*.nds; do [ -f "$f" ] && { ROM="$f"; break; }; done
    [ -z "$ROM" ] && [ -f rom/last-rom.txt ] && ROM="$(tr -d '\r' < rom/last-rom.txt)"
fi
if [ -z "$ROM" ] || [ ! -f "$ROM" ]; then
    echo "No ROM found. Put your Dragon Quest IX ROM (.nds) in the \"rom\" folder,"
    echo "or give it: ./BUILD.sh /path/to/your-rom.nds"
    exit 1
fi
case "$ROM" in /*) ;; *) ROM="$(pwd)/$ROM" ;; esac
[ -n "$DQIX_PROGRESS" ] && echo "progress 0.02 Checking your ROM"
echo "ROM: \"$ROM\""
mkdir -p rom && printf '%s\n' "$ROM" > rom/last-rom.txt

# ---- the build tools
command -v python3 >/dev/null 2>&1 || { echo "Python 3 is needed: install your distribution's python3 package, then run this again."; exit 1; }
[ -n "$DQIX_PROGRESS" ] && echo "progress 0.05 Checking the build tools (the first time may download some)"
echo "Checking the build tools..."
TOOLS="$(python3 tools/linux_tools.py build/linux-tools)" || { echo; echo "BUILD FAILED: the build tools are missing (the text above says which)."; exit 1; }
eval "$TOOLS"

# ---- build
SH=sh; command -v bash >/dev/null 2>&1 && SH=bash
if ! $SH tools/build-game.sh "$ROM"; then
    echo
    echo "BUILD FAILED. The text above says why."
    exit 1
fi
echo
echo "Finished. Play with game/play.sh (or game/dqix)."
