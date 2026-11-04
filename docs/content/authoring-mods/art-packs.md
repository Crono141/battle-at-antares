# HD art packs

Battle at Antares draws every screen at 4x the original 640x480 (2560x1920) and can show
high-resolution art in place of the original sprites. An **art pack** is an ordinary package (see
[Packages and manifests](packages-and-manifests.md)) whose only content is images under
`data/art/`. Art-only packages change how the game looks, not how it plays, so saves don't record
them: a save loads with or without your pack, and the player's enabled art packs stay active when
it does.

The bundled HD pack is itself one: `antares-hd`.

## Folder layout

```text
data/mods/<mod-id>/
  mod.json5                  id, name, version, requires.engine, config
  config/main.cfg            required, may be empty
  data/art/<LBX>/<EEE>/      one folder per LBX entry: upper-case file name, 3-digit entry number
      000.png 001.png ...    one image per frame of the entry (or .webp)
      000.mask.png ...       optional, per frame: which pixels take the game's effects (below)
      sprite.json            optional: scale, origins, shared frames, masks
  data/art/index.json        optional, recommended for distribution (see Packaging)
```

For example `data/art/CMBTSHP.LBX/020/000.webp` replaces frame 0 of entry 20 of `CMBTSHP.LBX` (a
combat ship). Packs from before this version used `data/hd/`; it is still read.

Enable the pack like any other: `enable <mod-id>;` in `data/antares.cfg` or a profile. The first
enabled pack wins per sprite, so a small pack can sit above `antares-hd` and replace a few sprites.

## Images

- **Scale.** An image is drawn over the original frame at 1x, 2x or 4x its size (3x for full-colour
  art). Without a `sprite.json` the scale is the image size divided by the original frame size,
  which must come out exactly.
- **Full colour** (RGB or RGBA PNG, or WebP) is the one format: drawn in its own colours, with a
  mask (below) wherever pixels must follow the game. Paletted PNGs from paint programs are full
  colour too: their palette order is never trusted. Packs made before this version may hold grey
  (L or L+A) PNGs whose grey value is the palette index; the engine still reads them, and
  `tools/artconvert.py` turns them into full colour plus masks without changing how they look.
- **Transparency** is the alpha channel; soft edges are fine.

## Masks: keeping the game's colour effects

The game changes some colours while it runs: a combat ship takes its owner's colour, a fighter
its side's colour, a colony building the planet's climate colours, beam flashes each weapon's
colour, and glass pixels tint whatever is under them. A full-colour image doesn't know about
any of that. A **mask** tells the engine which pixels follow the game:

`NNN.mask.png` is a grey image the size of frame `NNN`. Its value per pixel:

| Value | Meaning |
| --- | --- |
| 0 | the pixel's own colour |
| 1 - 239 | a **shade** of that palette index: the pixel shows the game's current colour for that index (after player-colour recolours and palette changes), lighter or darker as your art is relative to the frame's average for that value, so your detail stays |
| 240 - 255 | **glass** of level value - 240, for sprites drawn as glass: it tints what is under it, as the game's glass does |

So paint player-colour areas with light and shade only; the game supplies the hue. With a
`sprite.json`, masks are only read when it says `"mask": true`.

Masked pixels stay tied to their index until the screen is shown: palette **cycling** (colours
that pulse every frame, for example index 255 on the colony screen) and recolours the game makes
after drawing a sprite into a picture (the colony build screen greys its ship) reach them too.

A frame whose every drawn pixel is masked, with one colour per mask value, is **indexed art**:
each pixel is exactly its index, as in the original game. That is how the tools write art that
was made in the palette (`artconvert.py`, `artkit.py import --indexed`).

## sprite.json

Needed when frames differ in size, when art reaches past the original's box (glows, soft edges),
or when frames repeat. The import tool always writes one.

```json5
{
  "scale": 4, "format": "rgba", "image": "webp", "mask": true,
  "frames": [
    {"frame": 0, "origin": [-2, -1], "size": [252, 248]},   // origin in original pixels, size in image pixels
    {"frame": 1, "same_as": 0}
  ]
}
```

A frame listed with neither `origin` nor `same_as` keeps the original pixels (enlarged).

## The artist workflow (artkit)

The Moo2Mods tools (`tools/artkit.py`) do the bookkeeping:

1. **Export** what you want to change, with the current HD art as the starting point:

   ```text
   tools/artkit.py export CMBTSHP.LBX/020 my-export
   tools/artkit.py export 'COL*.LBX' my-export
   ```

   Each sprite folder gets `1x/` (the original frames, in the colours of the screen they appear on),
   `4x/` (the frames to edit, on one canvas), the masks beside them, `palette.gpl` (the sprite's
   palette for GIMP, Krita, Aseprite or Inkscape), and a `README.txt` saying which effects apply and
   which colours the game changes.

2. **Paint** the `4x/NNN.png` files in any program. Keep the file names and canvas size. Leave
   frames you don't change untouched: the import tool recognises them by their hash. If you change
   a shape that has a mask, update the mask too; if you delete a mask, the tool takes the pack's
   own mask for that frame, or rebuilds one from the palette colours your picture uses.

3. **Import** the folder into a mod:

   ```text
   tools/artkit.py import my-export data/mods/my-art --name "My art" [--zip] [--indexed]
   ```

   Unchanged frames are copied from the pack exactly; edited ones are written in full colour with
   their masks (`--indexed` quantises every pixel to the palette, written with every pixel masked). The tool writes `mod.json5`,
   `config/main.cfg`, `data/art/` and the index.

4. **Test** with a profile that enables your mod above the HD pack,
   `data/profiles/my-art.cfg`:

   ```text
   include ../antares.cfg;
   enable my-art;
   enable antares-hd;
   ```

   and start the game with `-config profiles/my-art.cfg`. Add `render art_hot_reload = true;` to
   the profile while you work: changed art files are picked up while the game runs (on a local
   disk; polling is costly over a network share). In hot reload mode the index is ignored, so new
   folders show up too.

## Packaging

`data/art/index.json` lists every sprite.json of the pack in one file; the engine reads it once at
start instead of one file per sprite, which matters on network drives. When an index exists the
engine trusts it completely, so rebuild it after every change (`tools/hdindex.py data/mods/my-art/data/art`;
the import tool does this). A pack can ship as a folder or as `<mod-id>.zip` with the same
layout inside.
