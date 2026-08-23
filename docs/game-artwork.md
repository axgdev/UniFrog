# Game Artwork (Thumbnails)

UniFrog can show box art, screenshots, and short text descriptions next to
the game list. This guide explains where to put the files on your SD card.

## Quick start with muOS artwork packs

UniFrog reads the same layout as [MustardOS](https://muos.dev/installation/artwork),
so ready-made packs such as
[antiKk/muOS-Artwork](https://github.com/antiKk/muOS-Artwork) work directly.
Generate the pack with the Skraper mixes it ships (the mixes output 320x240
images, which is ideal for this device), then copy the `MUOS` folder to the
root of your SD card:

```text
SD card root
└── MUOS
    └── info
        └── catalogue
            ├── Nintendo SNES-SFC
            │   ├── box
            │   │   └── Super Mario World.png
            │   ├── preview
            │   │   └── Super Mario World.png
            │   └── text
            │       └── Super Mario World.txt
            └── Nintendo Game Boy Advance
                └── box
                    └── Metroid Fusion.png
```

Rules:

1. The image file name must match the ROM file name, e.g. ROM
   `Chrono Trigger (USA).zip` needs `Chrono Trigger (USA).png`.
2. Box art goes in `box/`, screenshots in `preview/`, and optional
   descriptions in `text/<rom name>.txt`. All live directly inside the
   system folder - no subfolders.
3. The system folder is matched against your ROM folder name. Short names
   such as `SNES`, `GBA`, `MD`, `PS1`, or `NEOGEO` are translated
   automatically (`SNES` -> `Nintendo SNES-SFC`, and so on). If a folder name
   already matches a catalogue folder exactly, that is used first.

## UniFrog-native location

The same files can also live under `unifrog_data/artwork/`, which is checked
before the `MUOS` folders:

```text
SD card root
└── unifrog_data
    └── artwork
        └── SNES
            ├── box
            │   └── Super Mario World.png
            ├── preview
            │   └── Super Mario World.png
            └── text
                └── Super Mario World.txt
```

Here the system folder is used as-is, so this also works for any custom
folder names.

## Other layouts

Config -> Appearance -> ROM Artwork cycles three layouts:

- `muos` (default): the locations above.
- `skraper`: images inside each ROM folder under `media/images/`,
  `media/screenshots/`, and friends, as produced by Skraper's
  "user/media" output style.
- `beside`: images next to each ROM named `<rom>.box.png`,
  `<rom>.preview.png`, and `<rom>.txt`.

The current templates are shown under Config -> Appearance -> ROM Artwork
(sub-screen "Box Art" / "Game Preview" / "Game Text"). Advanced users can
override them by setting `artwork_box_templates`, `artwork_preview_templates`,
and `artwork_text_templates` in `unifrog_data/unifrog.ini`
(`|` separates fallback paths; `{system}`, `{name}`, `{filename}`, and
`{rom_dir}` are expanded).

## Hiding artwork

Box art shows by default. Toggle Config -> Appearance -> Box Art Hide (or set
`boxart_hidden=1` in `unifrog_data/unifrog.ini`) to hide the art pane without
deleting anything.

## Supported files and limits

- Formats: PNG (including interlaced), JPG/JPEG, BMP; SVG for themes.
- Keep images at or below 320x320; larger images are shrunk automatically,
  which costs loading time.
- Very large PNGs (over 640x480) decode through a slower generic path, so
  prefer pre-sized images for fast scrolling.

## Troubleshooting

- Nothing shows: check the file name matches the ROM name exactly
  (case-insensitive), including spaces and parentheses.
- Only some games show art: those entries simply have no matching file yet.
- Wrong image: a file in `unifrog_data/artwork/<folder>/` wins over the
  `MUOS` catalogue; check there first.
