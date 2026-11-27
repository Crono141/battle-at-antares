# Application icon

The Antaran's face (owner, 2026-11-25), captured from the game: `antares-source.png` is the square
crop (290 px, above the mouse pointer in the capture). Made with ImageMagick:

- `antares-64.png` to `antares-512.png`: the crop, Lanczos.
- `antares-16.png` to `antares-48.png`: a tighter crop on the head (200 px at 65,40 of the capture),
  lightly sharpened, so the face still reads at small sizes.
- `antares.ico` (16 to 256, Windows: antares.exe, antares-launcher.exe via
  `resources/windows/antares.rc.in`, the installer), `antares.icns` (128/256/512 PNG entries, the
  macOS app), `antares-128.png` (the launcher's window icon, embedded), `antares-256.png` (the
  Linux desktop entry, `antares.png` in the archive; the Godot project's `icon.png`).
