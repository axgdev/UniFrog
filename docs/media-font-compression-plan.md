# Media, Fonts, and Compressed Content

This document records the current UniFrog policy for features that can easily
inflate `unifrog.bin`.

## Media playback

UniFrog should prefer the HCRTOS `hcplayer` path before adding software
decoders. The current SDK already links the player, hardware/video plugins, the
image decoder plugin, and audio plugins for formats such as MP3 and Opus. The
frontend therefore treats these as media files:

```text
.mp4 .mov .mkv .avi .ts .m2ts .mpg .mpeg .h264 .264
.mp3 .wav .flac .ogg .opus .aac .m4a
.jpg .jpeg .png .gif .bmp
```

Audio-only files disable video presentation. Still images use the player image
display mode and stay open until the user exits. Video/image files keep the
speaker gate closed unless the player reports a real audio stream.

If a format works through `hcplayer`, do not copy a software decoder into the
main binary. If a software decoder is unavoidable and large, build it as an
external module under `/media/mmcblk0/unifrog/modules` rather than linking it
into `unifrog.bin`.

## Fonts

The fast built-in renderer is still the fixed 5x7 bitmap path, with optional
`.ufnt` overrides from the theme file:

```ini
font=/media/mmcblk0/unifrog/font.ufnt
```

Runtime TTF support is also available through the same theme key:

```ini
font=/media/mmcblk0/unifrog/themes/my-font.ttf
```

This uses upstream `stb_truetype` rather than the older copy bundled in the
HCRTOS tree. The backend bakes printable ASCII into a small atlas when the
theme is loaded. The 5x7 path remains the fallback and is still used for scale-2
diagnostics, loading screens, and exception screens.

The local HCRTOS tree has a FreeType build recipe but this SDK does not include
a ready FreeType header/library. If FreeType is ever used, add it to
`hcrtos-sdk` or ship it as an external module. Do not replace the fallback
bitmap renderer with FreeType-only drawing.

## Compressed ROMs

Compressed ROM support is frontend-owned where possible:

- `.zip`: single-ROM archives are supported for cores that accept memory
  content. UniFrog parses the archive, picks the first entry matching the
  selected core's `valid_extensions`, inflates method 8/stored method 0 with
  the SDK zlib, verifies CRC32, and passes the resulting buffer through
  `retro_game_info.data`.
- `.lz4`, `.zst`, and `.zstd`: UniFrog tries the RAM path first for cores that
  accept memory content. It reads the compressed file, checks the advertised
  uncompressed size, allocates a 32-byte-aligned ROM buffer, decompresses
  directly into that buffer, and frees the compressed input before
  `retro_load_game()`.
- For cores that accept memory content, decompressed ROM buffers are 32-byte
  aligned before `retro_load_game()`. This keeps MIPS dynarec and unaligned
  memory-handler paths from depending on allocator luck.
- For cores that require a full path, use the extracted-file cache because the
  libretro API cannot hand them a real filesystem path for an in-memory buffer.
  Cache filenames are deterministic hashes of the source path and selected
  entry, with the inner ROM extension preserved so core extension checks still
  work. Large PSX images should generally stay uncompressed on-device unless
  the user accepts disk-to-disk extraction time.

gpSP is the one current core branch change that alters the content-loading
contract: under `SF2000` it accepts `retro_game_info.data` and maps that buffer
into its existing 1 MB gamepak pages. That keeps compressed GBA ROMs on the
fast UniFrog RAM-decompression path while preserving the normal file path for
save/config naming.

The runtime only needs decompression. On-device compression would require
linking larger encoder code into the base firmware and is not currently worth
the binary growth. Author `.lz4`/`.zst`/`.zstd` ROMs off-device and keep the
inner filename extension, for example `game.gba.zst`; generic names such as
`game.zst` cannot be auto-classified without a manual core choice.

Do not push archive support into individual libretro core branches unless the
upstream core already has a standard archive path. Keeping this in UniFrog makes
new cores easier to sync.
