#!/usr/bin/env python3
"""build.mk without make, for a Linux that has no make (SteamOS on the Steam
Deck). tools/build-game.sh runs it with the same VAR=value arguments it gives
make; only the Linux SDL build (the game and dqixcore.so) is done here.

Keep it in step with build.mk: the same files, the same flags. Like make, a
file is compiled again only when it is newer than its object (or a runtime
header changed, for the runtime's files).
"""
import glob, os, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

V = dict(a.split("=", 1) for a in sys.argv[1:] if "=" in a)
CC = shlex.split(os.environ.get("CC") or "gcc")
GEN, GEN7, OBJ = V.get("GEN", "../build/recomp"), V.get("GEN7", "../build/recomp7"), V.get("OBJ", "obj")
OUT, CORE = V.get("OUT", "dqix"), V.get("CORE")
PIC, DEFS = shlex.split(V.get("PIC", "")), shlex.split(V.get("DEFS", ""))
CFLAGS = shlex.split(V.get("CFLAGS", "-O1 -g -w"))
SDL_CFLAGS, SDL_LIBS = shlex.split(V.get("SDL_CFLAGS", "")), shlex.split(V.get("SDL_LIBS", ""))
DETERMINISM = ["-ftrivial-auto-var-init=zero"]

SDL_ONLY = {"gl_render", "menu_win32", "gamemenu", "controls"}
rt = sorted(p for p in glob.glob("runtime/*.c")
            if os.path.basename(p)[:-2] not in SDL_ONLY | {"stub_runtime", "exe_main"}
            and not os.path.basename(p).startswith("frontend_"))
slirp = sorted(glob.glob("third_party/libslirp/src/*.c")) + ["third_party/libslirp/glib/glib.c"]
SLIRP_INC = ["-Ithird_party/libslirp/src", "-Ithird_party/libslirp/glib"]
headers = max((os.path.getmtime(h) for h in glob.glob("runtime/*.h")), default=0)


def rt_flags(name):
    if name in SDL_ONLY:
        return ["-O2", "-g", "-Wall", "-Wno-unused-parameter", *PIC, *SDL_CFLAGS, *DEFS, "-Iruntime"]
    if name == "frontend_sdl":
        return ["-O2", "-g", "-Wall", "-Wno-unused-parameter", *PIC, *DETERMINISM, *SDL_CFLAGS, *DEFS, "-Iruntime"]
    if name == "net":
        return ["-O2", "-g", "-Wall", *PIC, "-DLIBSLIRP_STATIC_BUILD", *DEFS, *SLIRP_INC, "-Iruntime"]
    return ["-O2", "-g", "-Wall", "-Wextra", "-Wno-unused-parameter", *PIC, *DETERMINISM, *DEFS, "-Iruntime", f"-I{GEN}"]


jobs = {}   # object -> (source, flags, also-newer-than)
for src in glob.glob(f"{GEN}/*.c"):
    jobs[f"{OBJ}/gen/{os.path.basename(src)[:-2]}.o"] = (src, [*CFLAGS, *PIC, *DEFS, "-Iruntime", f"-I{GEN}"], 0)
for src in glob.glob(f"{GEN7}/*.c"):
    jobs[f"{OBJ}/gen7/{os.path.basename(src)[:-2]}.o"] = (src, [*CFLAGS, *PIC, *DEFS, "-Iruntime", f"-I{GEN7}"], 0)
for src in rt + [f"runtime/{n}.c" for n in sorted(SDL_ONLY)] + ["runtime/frontend_sdl.c", "runtime/frontend_none.c", "runtime/exe_main.c"]:
    n = os.path.basename(src)[:-2]
    jobs[f"{OBJ}/rt/{n}.o"] = (src, rt_flags(n), headers)
for src in slirp:
    jobs[f"{OBJ}/slirp/{src[len('third_party/libslirp/'):-2]}.o"] = (
        src, ["-O2", "-w", *PIC, "-DLIBSLIRP_STATIC_BUILD", "-DBUILDING_LIBSLIRP", '-DG_LOG_DOMAIN="Slirp"', *SLIRP_INC], 0)


def stale(o, src, extra):
    return not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(src), extra)


def compile_one(o):
    src, flags, _ = jobs[o]
    os.makedirs(os.path.dirname(o), exist_ok=True)
    cmd = [*CC, *flags, "-c", src, "-o", o]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        return f"{' '.join(cmd)}\n{r.stdout}{r.stderr}"
    return None


todo = [o for o, (s, _, x) in jobs.items() if stale(o, s, x)]
print(f"   compiling {len(todo)} of {len(jobs)} files", flush=True)
with ThreadPoolExecutor(os.cpu_count() or 2) as ex:
    for i, err in enumerate(ex.map(compile_one, todo), 1):
        if err:
            sys.exit(err)
        if i % 25 == 0 or i == len(todo):
            print(f"   {i}/{len(todo)}", flush=True)

gen = sorted(o for o in jobs if o.startswith((f"{OBJ}/gen/", f"{OBJ}/gen7/")))
sl = sorted(o for o in jobs if o.startswith(f"{OBJ}/slirp/"))
rto = [f"{OBJ}/rt/{os.path.basename(s)[:-2]}.o" for s in rt]


def link(out, objs, pre, post):
    if os.path.exists(out) and os.path.getmtime(out) >= max(os.path.getmtime(o) for o in objs):
        return
    print(f"   linking {out}", flush=True)
    subprocess.run([*CC, *pre, "-o", out, *objs, *post], check=True)


exe_objs = gen + rto + [f"{OBJ}/rt/{n}.o" for n in sorted(SDL_ONLY)] + [f"{OBJ}/rt/frontend_sdl.o"] + sl + [f"{OBJ}/rt/exe_main.o"]
link(OUT, exe_objs, [], [*SDL_LIBS, "-lm", *shlex.split(V.get("LDFLAGS", ""))])
if CORE:
    link(CORE, gen + rto + [f"{OBJ}/rt/frontend_none.o"] + sl, ["-shared"], ["-lm", "-Wl,-Bsymbolic", "-lpthread"])
