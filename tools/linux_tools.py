#!/usr/bin/env python3
"""The build tools BUILD.sh needs that this Linux doesn't have, fetched into
build/linux-tools/ for this folder only (nothing installed in the system, no
administrator password). It is how the game builds on a Steam Deck, whose
SteamOS has Python and SDL2 but no C compiler, no make and no SDL2 headers.

  tools/linux_tools.py <dir>

Prints shell lines (export ...) for BUILD.sh to run. Each piece is fetched
only when missing, once, and checked against its SHA-256:
  PyYAML     the recompiler reads the symbol map with it (its pure-Python part)
  SDL2       its headers, to compile against the SDL2 the system already has
  Zig        a C compiler in one folder (about 80 MB), when there's no gcc/cc
"""
import ctypes.util, hashlib, io, os, shutil, stat, subprocess, sys, tarfile, urllib.request, zipfile

D = os.path.abspath(sys.argv[1])
os.makedirs(D, exist_ok=True)

YAML = ("https://files.pythonhosted.org/packages/75/e4/2c27590dfc9992f73aabbeb9241ae20220bd9452df27483b6e56d3975cc5/"
        "PyYAML-6.0.2-cp311-cp311-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
        "3ad2a3decf9aaba3d29c8f537ac4b243e36bef957511b4766cb0057d32b0be85")
SDL = ("https://github.com/libsdl-org/SDL/releases/download/release-2.28.5/SDL2-2.28.5.tar.gz",
       "332cb37d0be20cb9541739c61f79bae5a477427d79ae85e352089afdaf6666e4")
ZIG = ("https://files.pythonhosted.org/packages/0c/b7/ecfdce6368cc3664d301f7f52db4fe1004aa7da7a12c4a9bf1de534ff6ab/"
       "ziglang-0.13.0-py3-none-manylinux_2_12_x86_64.manylinux2010_x86_64.musllinux_1_1_x86_64.whl",
       "3ce0c9f16547e5d61b32e0d226926e9a2552ef4b91fccf7ab5ea1a623a77824b")


def say(s):
    print(s, file=sys.stderr, flush=True)


def fetch(what, url_sha):
    url, sha = url_sha
    say(f"   downloading {what}...")
    data = urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "dqix-build"}), timeout=120).read()
    if hashlib.sha256(data).hexdigest() != sha:
        sys.exit(f"   {what}: the download is damaged (wrong SHA-256); run BUILD.sh again")
    return data


def have(cmd):
    return shutil.which(cmd) is not None


out = []

# PyYAML: only the .py files (the C speed-up is for one Python version; yaml works without it)
try:
    import yaml  # noqa: F401
except ImportError:
    py = os.path.join(D, "python")
    if not os.path.isfile(os.path.join(py, "yaml", "__init__.py")):
        z = zipfile.ZipFile(io.BytesIO(fetch("PyYAML", YAML)))
        for n in z.namelist():
            if n.startswith("yaml/") and n.endswith(".py"):
                z.extract(n, py)
    out.append(f'export PYTHONPATH="{py}${{PYTHONPATH:+:$PYTHONPATH}}"')

# SDL2: the system's library, with headers from SDL's own source release
if subprocess.run("sdl2-config --cflags", shell=True, capture_output=True).returncode != 0:
    lib = ctypes.util.find_library("SDL2-2.0") or ctypes.util.find_library("SDL2")
    if not lib:
        sys.exit("   SDL2 isn't on this computer: install your distribution's SDL2 package (libsdl2 / SDL2), then run BUILD.sh again")
    path = None
    for d in ("/usr/lib/x86_64-linux-gnu", "/usr/lib64", "/usr/lib", "/lib/x86_64-linux-gnu", "/lib64", "/lib"):
        if os.path.exists(os.path.join(d, lib)):
            path = os.path.join(d, lib)
            break
    if not path:
        sys.exit(f"   found SDL2 ({lib}) but not its folder; install your distribution's SDL2 development package")
    inc = os.path.join(D, "SDL2", "include")
    if not os.path.isfile(os.path.join(inc, "SDL.h")):
        t = tarfile.open(fileobj=io.BytesIO(fetch("SDL2's headers", SDL)), mode="r:gz")
        os.makedirs(inc, exist_ok=True)
        for m in t.getmembers():
            parts = m.name.split("/")
            if len(parts) == 3 and parts[1] == "include" and m.isfile() and parts[2].endswith(".h"):
                with open(os.path.join(inc, parts[2]), "wb") as f:
                    f.write(t.extractfile(m).read())
    out.append(f'export SDL_CFLAGS="-I{inc} -D_REENTRANT"')
    out.append(f'export SDL_LIBS="{path} -lpthread -ldl"')

# a C compiler
if not (have("gcc") or have("cc") or have("clang")):
    zig = os.path.join(D, "ziglang", "zig")
    if not os.path.isfile(zig):
        z = zipfile.ZipFile(io.BytesIO(fetch("the Zig C compiler (about 80 MB)", ZIG)))
        for n in z.namelist():
            if n.startswith("ziglang/"):
                z.extract(n, D)
        os.chmod(zig, os.stat(zig).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    # its cache stays in this folder too
    out.append(f'export ZIG_GLOBAL_CACHE_DIR="{D}/zig-cache" ZIG_LOCAL_CACHE_DIR="{D}/zig-cache"')
    out.append(f'export CC="{zig} cc"')
elif not have("gcc"):
    out.append(f'export CC="{"cc" if have("cc") else "clang"}"')

print("\n".join(out))
