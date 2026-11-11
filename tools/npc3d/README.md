# Townspeople in 3D (runtime/npc3d.c): model converter

Makes the `npc3d/<model>.npc3` files the runtime draws in place of a DQIX
townsperson's flat picture, from the player's own Dragon Quest VII (3DS)
romfs. Nothing made here is ever committed (game-derived).

- `lz11.py`: LZ11 decompression of the romfs files.
- `cgfx.py`: CGFX model reader (meshes, skin, bones, textures).
- `canm.py`: CGFX skeletal animations (curve and baked tracks).
- `pose.py`: poses a model (skinning) at a time of an animation.
- `paint.py`: repaints a model's texture with a DQIX townsperson's colours,
  from its front picture.
- `npc3d_export.py`: bakes a model's idle and run animations to `.npc3`
  (`--sprite <front picture.png>` to repaint it).

`npc3d/map.txt` in the game folder holds one `<DQIX sprite type> <model>`
line per townsperson, e.g. `n008a p0229-n008a`. Option: `npc_3d = 1` in
settings.ini (enhanced renderer only).
