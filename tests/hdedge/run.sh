#!/bin/sh
# pack_fix_edges (runtime/gl_render.c) on small made-up pictures: sh tests/hdedge/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
tmp=$(mktemp -d)
awk '/^static void pack_fix_edges/,/^}/' "$here/../../runtime/gl_render.c" > "$tmp/fix.inc"
${CC:-gcc} -O1 -I "$tmp" "$here/t_hdedge.c" -o "$tmp/t"
"$tmp/t"
