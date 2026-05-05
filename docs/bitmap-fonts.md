# Bitmap Fonts

UniFrog defaults to the built-in 5x7 bitmap font for the English UI. It is
fast, sharp, and does not allocate font file memory. Users can switch to the
packaged Spleen BDF bitmap fonts for larger Latin text, or to the packaged TTF
fonts when they need broader Unicode coverage.

## Candidate Sources

- Spleen provides BDF bitmap fonts in 5x8, 6x12, 8x16, and larger sizes under
  BSD-2-Clause. UniFrog packages `Spleen6x12.bdf` and `Spleen8x16.bdf` for
  Latin UI text, Latin-1 ROM names, box drawing, and CP437-style labels.
- GNU Unifont provides BDF and PCF releases around 1 MB with very broad BMP
  coverage, and current releases are dual licensed under SIL OFL 1.1 and
  GPLv2+ with the GNU font embedding exception. UniFrog packages the compressed
  BDF as source material for generated locale subsets. Loading the full BDF
  directly is intentionally avoided because the expanded glyph set is too large
  for the frontend heap.
- u8g2 includes many embedded bitmap font groups and the library code is
  BSD-2-Clause, but individual font groups keep their original licenses. Any
  imported glyph set must be attributed at the font-group level.

## Renderer Notes

Adding a bitmap Unicode font is not enough for every supported language.
Arabic, Urdu, Hindi, Bengali, Marathi, Tamil, and Telugu need shaping and, for
Arabic/Urdu, bidirectional layout. Until UniFrog has that layout layer, those
languages should continue to use curated TTF fallbacks or pre-shaped UI strings.

The preferred next step is to generate compact UniFrog bitmap font packs from
the packaged Unifont source, subset per locale, and load those packs at
runtime. That keeps runtime rendering cheap while preserving license
attribution and avoiding multi-megabyte OTF files in the SD package.
