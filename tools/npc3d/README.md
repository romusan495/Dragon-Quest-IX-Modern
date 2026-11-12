# Townspeople in 3D (runtime/npc3d.c): model converter

Makes the `npc3d/<model>.npc3` files the runtime draws in place of a DQIX
townsperson's flat picture, from the player's own Dragon Quest VII and/or
VIII (3DS), put in `rom/3DS` (decrypted .3ds/.cci/.cxi/.cia, or an extracted
romfs folder; BUILD.bat creates the folder) or named in `rom/dq7-path.txt`,
`rom/dq8-path.txt`,
with those games' own colours. `townsfolk.txt` says which character replaces
which townsperson (`n008a dq7 p0229`). Nothing made here is ever committed
(game-derived).

- `rom3ds.py`: finds DQVII / DQVIII in `rom/3DS` and copies the folders needed out of a decrypted ROM (NCSD, NCCH, CIA; romfs) into `build/npc3d-src`.
- `lz11.py`: LZ11 decompression of the romfs files.
- `cgfx.py`: CGFX model reader (meshes, skin, bones, textures).
- `canm.py`: CGFX skeletal animations (curve and baked tracks).
- `pose.py`: poses a model (skinning) at a time of an animation.
- `bch.py`: DQVIII BCH model reader (meshes, skin, bones, textures, skeletal
  animations); of the alternate hand meshes (fist, open, pointing), keeps the open one.
- `bch_export.py`: bakes a DQVIII model's idle and walk animations to `.npc3`.
- `nds.py`, `nsbmd.py`: files of the DQIX ROM and its NARC archives; Nitro NSBMD models and NSBCA animations (the story characters, `/data/chara_sub/*.chr`).
- `nsbmd_export.py`: bakes a DQIX character's stand and walk animations to `.npc3` (from the ROM being built, `dq9` in `townsfolk.txt`).
- `make_all.py`: every line of `townsfolk.txt` (BUILD.bat).
- `paint.py` (unused since the games' own colours are kept): repaints a model's texture with a DQIX townsperson's colours,
  from its front picture.
- `npc3d_export.py`: bakes a model's idle and run animations to `.npc3`
  (`--front <picture.png> --back <picture.png>`: repaint it with the DQIX
  townsperson's colours, seen from the front and from the back; its arms
  follow the front picture's arms down their length).

`npc3d/map.txt` in the game folder holds one `<DQIX sprite type> <model>`
line per townsperson, e.g. `n008a p0229-n008a`. Option: `npc_3d = 1` in
settings.ini (enhanced renderer only).
