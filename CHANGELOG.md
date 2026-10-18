# Changelog

## 1.0.0

First public release.

- Open VTF 7.0 – 7.5 in almost every format; alpha as an alpha channel.
- Animated textures open as layers (frame order: bottom-up or top-down).
- Save DXT1/3/5, BGR(A)8888, RGB(A)8888, 565/4444/5551, I8, IA88, A8; *Auto* formats pick
  DXT1/DXT5 (or BGR888/BGRA8888) by alpha. Versions 7.1 – 7.5.
- Visible layers → animation frames.
- Mipmaps with 15 filters, optional gamma-correct filtering; 16×16 DXT1 thumbnail.
- Resize to a power of two with filter and max size.
- Texture flags; one/eight-bit alpha flags set automatically.
- Re-saving an opened VTF keeps its format, version, mipmaps, flags and frame order.
- Interface in English, Russian and German; translations embedded from `lang/` and loaded from
  the `VTFExport_lang` folder next to the plug-in.
